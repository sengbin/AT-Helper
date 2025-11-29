/*------------------------------------------------------------------------
名称：应用入口实现
说明：实现 WinMain、主对话框及界面交互逻辑
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-29
备注：无
------------------------------------------------------------------------*/
#include "AppEntry.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <cwchar>
#include <iterator>
#include <sstream>
#include <vector>
#include <Richedit.h>
#include <Commctrl.h>

#pragma comment(lib, "Comctl32.lib")

#ifndef WM_CTLCOLORCOMBOBOX
#define WM_CTLCOLORCOMBOBOX 0x0134
#endif

namespace
{
    constexpr UINT WM_APP_LOGTEXT = WM_APP + 100;
    constexpr UINT WM_APP_SMS_TEXT = WM_APP + 101;

    void CenterWindow(HWND window)
    {
        RECT rc{};
        RECT parent{};
        HWND desktop = GetDesktopWindow();
        if (!GetWindowRect(window, &rc) || !GetWindowRect(desktop, &parent))
        {
            return;
        }
        const int width = rc.right - rc.left;
        const int height = rc.bottom - rc.top;
        const int x = parent.left + ((parent.right - parent.left) - width) / 2;
        const int y = parent.top + ((parent.bottom - parent.top) - height) / 2;
        SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
    }

    std::wstring TrimCopy(std::wstring text)
    {
        const auto notSpaceFront = [](wchar_t ch)
        {
            return std::iswspace(static_cast<unsigned short>(ch)) == 0;
        };
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpaceFront));
        text.erase(std::find_if(text.rbegin(), text.rend(), notSpaceFront).base(), text.end());
        return text;
    }
}

AppController::AppController()
    : _instance(nullptr), _dialog(nullptr), _richEditModule(nullptr),
      _themeMode(ThemeMode::Light), _palette{}, _dialogBrush(nullptr), _controlBrush(nullptr)
{
}

AppController::~AppController()
{
    _session.SetLogCallback(nullptr);
    _session.SetSmsCallback(nullptr);
    _session.Disconnect();
    if (_richEditModule != nullptr)
    {
        FreeLibrary(_richEditModule);
        _richEditModule = nullptr;
    }
    if (_dialogBrush != nullptr)
    {
        DeleteObject(_dialogBrush);
        _dialogBrush = nullptr;
    }
    if (_controlBrush != nullptr)
    {
        DeleteObject(_controlBrush);
        _controlBrush = nullptr;
    }
}

bool AppController::Initialize(HINSTANCE instance)
{
    _instance = instance;
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);
    if (_richEditModule == nullptr)
    {
        _richEditModule = LoadLibraryW(L"Msftedit.dll");
        if (_richEditModule == nullptr)
        {
            MessageBoxW(nullptr, L"无法加载 Msftedit.dll，无法显示彩色日志", L"AT Helper", MB_ICONERROR | MB_OK);
            return false;
        }
    }
    _configPath = ResolveConfigPath();
    if (!_config.Load(_configPath))
    {
        MessageBoxW(nullptr, L"加载指令配置失败，已使用默认指令", L"AT Helper", MB_ICONWARNING | MB_OK);
    }
    _commands = _config.GetCommands();
    _smsProfile = _config.GetSmsProfile();
    _session.SetSmsProfile(_smsProfile);
    _themeMode = _config.GetTheme();
    ApplyTheme(_themeMode);
    return true;
}

INT_PTR AppController::Run()
{
    return DialogBoxParamW(_instance, MAKEINTRESOURCEW(IDD_MAIN_DIALOG), nullptr, DialogRouter, reinterpret_cast<LPARAM>(this));
}

INT_PTR CALLBACK AppController::DialogRouter(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG)
    {
        auto* self = reinterpret_cast<AppController*>(lParam);
        if (self == nullptr)
        {
            return FALSE;
        }
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->_dialog = hWnd;
        return self->OnInitDialog(hWnd);
    }
    auto* self = reinterpret_cast<AppController*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (self == nullptr)
    {
        return FALSE;
    }
    return self->HandleDialogMessage(hWnd, message, wParam, lParam);
}

INT_PTR AppController::OnInitDialog(HWND hWnd)
{
    CenterWindow(hWnd);
    ResetSessionCallbacks();

    HWND logEdit = GetDlgItem(hWnd, IDC_EDIT_LOG);
    if (logEdit)
    {
        PARAFORMAT2 format{};
        format.cbSize = sizeof(format);
        format.dwMask = PFM_LINESPACING;
        format.bLineSpacingRule = 4; // 精确行距
        format.dyLineSpacing = 220;   // 略小于默认行距
        SendMessageW(logEdit, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&format));
    }

    InitializeThemeSelector();
    ApplyTheme(_themeMode);
    ApplyFlatBorderToControls();

    HWND baudCombo = GetDlgItem(hWnd, IDC_COMBO_BAUD);
    const std::array<unsigned long, 5> baudRates{9600, 19200, 38400, 57600, 115200};
    for (unsigned long rate : baudRates)
    {
        std::wstring text = std::to_wstring(rate);
        SendMessageW(baudCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    SendMessageW(baudCombo, CB_SETCURSEL, static_cast<WPARAM>(baudRates.size() - 1), 0);

    RefreshCommandList();
    RefreshPortList();
    SetStatus(L"未连接");
    return TRUE;
}

INT_PTR AppController::HandleDialogMessage(HWND, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        HandleCommand(wParam, lParam);
        return TRUE;
    case WM_CLOSE:
        DisconnectPort();
        EndDialog(_dialog, 0);
        return TRUE;
    case WM_APP_LOGTEXT:
        HandleLogPayload(reinterpret_cast<std::wstring*>(wParam));
        return TRUE;
    case WM_APP_SMS_TEXT:
        HandleSmsPayload(reinterpret_cast<std::wstring*>(wParam));
        return TRUE;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        return HandleThemeColorMessage(message, wParam, lParam);
    default:
        break;
    }
    return FALSE;
}

void AppController::HandleCommand(WPARAM wParam, LPARAM lParam)
{
    const auto controlId = LOWORD(wParam);
    const auto notify = HIWORD(wParam);
    switch (controlId)
    {
    case IDC_BUTTON_CONNECT:
        if (notify == BN_CLICKED)
        {
            if (_session.IsConnected())
            {
                DisconnectPort();
            }
            else
            {
                TryConnectSelectedPort();
            }
        }
        break;
    case IDC_BUTTON_SEND_COMMAND:
        if (notify == BN_CLICKED)
        {
            wchar_t buffer[512]{};
            GetDlgItemTextW(_dialog, IDC_EDIT_COMMAND, buffer, static_cast<int>(std::size(buffer)));
            SendCommandText(buffer);
        }
        break;
    case IDC_BUTTON_SEND_SMS:
        if (notify == BN_CLICKED)
        {
            wchar_t numberBuffer[64]{};
            GetDlgItemTextW(_dialog, IDC_EDIT_SMS_NUMBER, numberBuffer, static_cast<int>(std::size(numberBuffer)));
            std::wstring targetNumber = TrimCopy(numberBuffer);
            wchar_t buffer[512]{};
            GetDlgItemTextW(_dialog, IDC_EDIT_SMS_TEXT, buffer, static_cast<int>(std::size(buffer)));
            if (targetNumber.empty())
            {
                MessageBoxW(_dialog, L"请填写短信号码", L"AT Helper", MB_OK | MB_ICONINFORMATION);
                return;
            }
            if (!_session.IsConnected())
            {
                MessageBoxW(_dialog, L"请先连接串口", L"AT Helper", MB_OK | MB_ICONINFORMATION);
                return;
            }
            SmsProfile profile = _smsProfile;
            profile.targetNumber = targetNumber;
            _session.SetSmsProfile(profile);
            _smsProfile.targetNumber = targetNumber;
            if (_session.SendSms(buffer))
            {
                SetDlgItemTextW(_dialog, IDC_EDIT_SMS_TEXT, L"");
            }
            else
            {
                MessageBoxW(_dialog, L"发送短信失败", L"AT Helper", MB_OK | MB_ICONWARNING);
            }
        }
        break;
    case IDC_BUTTON_RELOAD:
        if (notify == BN_CLICKED)
        {
            ReloadConfiguration();
        }
        break;
    case IDC_BUTTON_CLEAR_LOG:
        if (notify == BN_CLICKED)
        {
            SetDlgItemTextW(_dialog, IDC_EDIT_LOG, L"");
        }
        break;
    case IDC_COMBO_THEME:
        if (notify == CBN_SELCHANGE)
        {
            OnThemeSelectionChanged();
        }
        break;
    case IDC_COMMAND_LIST:
        if (notify == LBN_DBLCLK)
        {
            SendSelectedCommand();
        }
        break;
    default:
        break;
    }
}

void AppController::HandleLogPayload(std::wstring* payload)
{
    if (payload != nullptr)
    {
        AppendLog(*payload);
        delete payload;
    }
}

void AppController::HandleSmsPayload(std::wstring* payload)
{
    if (payload != nullptr)
    {
        AppendLog(*payload);
        delete payload;
    }
}

void AppController::RefreshCommandList()
{
    _commands = _config.GetCommands();
    _session.SetSmsProfile(_smsProfile);
    if (_dialog != nullptr)
    {
        SetDlgItemTextW(_dialog, IDC_EDIT_SMS_NUMBER, _smsProfile.targetNumber.c_str());
    }

    HWND list = GetDlgItem(_dialog, IDC_COMMAND_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& cmd : _commands)
    {
        std::wstring display = cmd.text;
        if (!cmd.summary.empty())
        {
            display.append(L" — ").append(cmd.summary);
        }
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
    }
}

void AppController::RefreshPortList()
{
    HWND combo = GetDlgItem(_dialog, IDC_COMBO_PORT);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    std::vector<std::wstring> ports;
    std::vector<wchar_t> buffer(4096);
    DWORD length = QueryDosDeviceW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        buffer.resize(32768);
        length = QueryDosDeviceW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length != 0)
    {
        wchar_t* current = buffer.data();
        while (*current != L'\0')
        {
            std::wstring name = current;
            if (name.rfind(L"COM", 0) == 0)
            {
                ports.push_back(name);
            }
            current += name.size() + 1;
        }
    }
    if (ports.empty())
    {
        for (int i = 1; i <= 8; ++i)
        {
            std::wstring name = L"COM";
            name.append(std::to_wstring(i));
            ports.push_back(std::move(name));
        }
    }
    std::sort(ports.begin(), ports.end());
    for (const auto& port : ports)
    {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(port.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void AppController::AppendLog(const std::wstring& text)
{
    if (_dialog == nullptr)
    {
        return;
    }
    HWND edit = GetDlgItem(_dialog, IDC_EDIT_LOG);
    if (!edit)
    {
        return;
    }

    const COLORREF color = ResolveLogColor(text);
    const int length = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, length, length);

    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = color;
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));

    std::wstring line;
    if (text.rfind(L"--> ", 0) == 0 && length > 0)
    {
        line.append(L"\r\n");
    }
    line.append(text);
    line.append(L"\r\n");
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));

    const int newLength = GetWindowTextLengthW(edit);
    if (newLength > 60000)
    {
        SendMessageW(edit, EM_SETSEL, 0, 20000);
        SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
    }

    const int finalLength = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, finalLength, finalLength);
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
    SendMessageW(edit, WM_VSCROLL, SB_BOTTOM, 0);
}

COLORREF AppController::ResolveLogColor(const std::wstring& text) const
{
    if (text.rfind(L"--> ", 0) == 0)
    {
        return _palette.sendTextColor;
    }
    if (text.rfind(L"<-- ", 0) == 0)
    {
        return _palette.receiveTextColor;
    }
    return _palette.logTextColor;
}

void AppController::SetStatus(const std::wstring& text)
{
    if (_dialog == nullptr)
    {
        return;
    }
    SetDlgItemTextW(_dialog, IDC_STATUS_TEXT, text.c_str());
}

bool AppController::TryConnectSelectedPort()
{
    const std::wstring port = GetSelectedPort();
    const unsigned long baud = GetSelectedBaud();
    if (port.empty() || baud == 0)
    {
        MessageBoxW(_dialog, L"请选择串口与波特率", L"AT Helper", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    if (!_session.Connect(port, baud))
    {
        MessageBoxW(_dialog, L"连接失败，请检查串口", L"AT Helper", MB_OK | MB_ICONERROR);
        return false;
    }
    std::wstringstream status;
    status << L"已连接 " << port << L" @ " << baud;
    SetStatus(status.str());
    SetDlgItemTextW(_dialog, IDC_BUTTON_CONNECT, L"断开");
    return true;
}

void AppController::DisconnectPort()
{
    if (_session.IsConnected())
    {
        _session.Disconnect();
    }
    SetDlgItemTextW(_dialog, IDC_BUTTON_CONNECT, L"连接");
    SetStatus(L"未连接");
}

void AppController::SendCommandText(const std::wstring& text)
{
    if (text.empty())
    {
        return;
    }
    if (!_session.IsConnected())
    {
        MessageBoxW(_dialog, L"请先连接串口", L"AT Helper", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!_session.SendCommand(text))
    {
        MessageBoxW(_dialog, L"发送失败", L"AT Helper", MB_OK | MB_ICONWARNING);
    }
}

void AppController::SendSelectedCommand()
{
    HWND list = GetDlgItem(_dialog, IDC_COMMAND_LIST);
    const int index = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (index >= 0 && static_cast<std::size_t>(index) < _commands.size())
    {
        SendCommandText(_commands[static_cast<std::size_t>(index)].text);
    }
}

std::wstring AppController::GetSelectedPort() const
{
    HWND combo = GetDlgItem(_dialog, IDC_COMBO_PORT);
    if (!combo)
    {
        return std::wstring();
    }
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0)
    {
        return std::wstring();
    }
    const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
    if (length <= 0)
    {
        return std::wstring();
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text.data()));
    text.resize(wcslen(text.c_str()));
    return text;
}

unsigned long AppController::GetSelectedBaud() const
{
    HWND combo = GetDlgItem(_dialog, IDC_COMBO_BAUD);
    if (!combo)
    {
        return 0;
    }
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0)
    {
        return 0;
    }
    const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
    if (length <= 0)
    {
        return 0;
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text.data()));
    text.resize(wcslen(text.c_str()));
    return static_cast<unsigned long>(wcstoul(text.c_str(), nullptr, 10));
}

void AppController::ReloadConfiguration()
{
    if (!_config.Load(_configPath))
    {
        MessageBoxW(_dialog, L"重新加载配置失败", L"AT Helper", MB_OK | MB_ICONWARNING);
    }
    _smsProfile = _config.GetSmsProfile();
    _themeMode = _config.GetTheme();
    ApplyTheme(_themeMode);
    RefreshCommandList();
    AppendLog(L"已重新加载指令配置");
}

void AppController::ResetSessionCallbacks()
{
    _session.SetLogCallback([this](const std::wstring& text)
    {
        if (_dialog == nullptr)
        {
            return;
        }
        auto* payload = new std::wstring(text);
        if (PostMessageW(_dialog, WM_APP_LOGTEXT, reinterpret_cast<WPARAM>(payload), 0) == 0)
        {
            delete payload;
        }
    });
    _session.SetSmsCallback([this](const std::wstring& header, const std::wstring& content)
    {
        if (_dialog == nullptr)
        {
            return;
        }
        auto* payload = new std::wstring(L"收到短信\r\n" + header + L"\r\n" + content);
        if (PostMessageW(_dialog, WM_APP_SMS_TEXT, reinterpret_cast<WPARAM>(payload), 0) == 0)
        {
            delete payload;
        }
    });
}

void AppController::InitializeThemeSelector()
{
    if (_dialog == nullptr)
    {
        return;
    }
    HWND combo = GetDlgItem(_dialog, IDC_COMBO_THEME);
    if (!combo)
    {
        return;
    }
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"浅色主题"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"深色主题"));
    UpdateThemeComboSelection();
}

void AppController::OnThemeSelectionChanged()
{
    if (_dialog == nullptr)
    {
        return;
    }
    HWND combo = GetDlgItem(_dialog, IDC_COMBO_THEME);
    if (!combo)
    {
        return;
    }
    const int selection = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (selection < 0)
    {
        return;
    }
    const ThemeMode desired = (selection == 1) ? ThemeMode::Dark : ThemeMode::Light;
    if (desired == _themeMode)
    {
        return;
    }
    ApplyTheme(desired);
    _config.SetTheme(desired);
    _config.Save(_configPath);
}

void AppController::ApplyTheme(ThemeMode mode)
{
    _themeMode = mode;
    _palette = BuildPalette(mode);
    RecreateThemeBrushes();

    if (_dialog == nullptr)
    {
        return;
    }

    UpdateThemeComboSelection();
    ApplyFlatBorderToControls();

    HWND logEdit = GetDlgItem(_dialog, IDC_EDIT_LOG);
    if (logEdit)
    {
        SendMessageW(logEdit, EM_SETBKGNDCOLOR, 0, _palette.logBackground);
        InvalidateRect(logEdit, nullptr, TRUE);
    }

    const std::array<int, 13> themedControls{
        IDC_COMMAND_LIST,
        IDC_EDIT_COMMAND,
        IDC_EDIT_SMS_NUMBER,
        IDC_EDIT_SMS_TEXT,
        IDC_COMBO_PORT,
        IDC_COMBO_BAUD,
        IDC_COMBO_THEME,
        IDC_STATUS_TEXT,
        IDC_BUTTON_CONNECT,
        IDC_BUTTON_RELOAD,
        IDC_BUTTON_CLEAR_LOG,
        IDC_BUTTON_SEND_COMMAND,
        IDC_BUTTON_SEND_SMS
    };
    for (int id : themedControls)
    {
        HWND control = GetDlgItem(_dialog, id);
        if (control)
        {
            InvalidateRect(control, nullptr, TRUE);
        }
    }

    InvalidateRect(_dialog, nullptr, TRUE);
}

void AppController::UpdateThemeComboSelection() const
{
    if (_dialog == nullptr)
    {
        return;
    }
    HWND combo = GetDlgItem(_dialog, IDC_COMBO_THEME);
    if (!combo)
    {
        return;
    }
    const int targetIndex = (_themeMode == ThemeMode::Dark) ? 1 : 0;
    SendMessageW(combo, CB_SETCURSEL, targetIndex, 0);
}

void AppController::RecreateThemeBrushes()
{
    if (_dialogBrush != nullptr)
    {
        DeleteObject(_dialogBrush);
        _dialogBrush = nullptr;
    }
    if (_controlBrush != nullptr)
    {
        DeleteObject(_controlBrush);
        _controlBrush = nullptr;
    }
    _dialogBrush = CreateSolidBrush(_palette.windowBackground);
    _controlBrush = CreateSolidBrush(_palette.controlBackground);
}

ThemePalette AppController::BuildPalette(ThemeMode mode) const
{
    ThemePalette palette{};
    if (mode == ThemeMode::Dark)
    {
        palette.windowBackground = RGB(28, 28, 28);
        palette.controlBackground = RGB(45, 45, 45);
        palette.textColor = RGB(230, 230, 230);
        palette.logBackground = RGB(20, 20, 20);
        palette.logTextColor = RGB(235, 235, 235);
        palette.sendTextColor = RGB(120, 180, 255);
        palette.receiveTextColor = RGB(160, 235, 160);
        palette.borderColor = RGB(96, 96, 96);
    }
    else
    {
        palette.windowBackground = RGB(244, 246, 249);
        palette.controlBackground = RGB(255, 255, 255);
        palette.textColor = RGB(32, 32, 32);
        palette.logBackground = RGB(255, 255, 255);
        palette.logTextColor = RGB(32, 32, 32);
        palette.sendTextColor = RGB(0, 120, 215);
        palette.receiveTextColor = RGB(0, 153, 0);
        palette.borderColor = RGB(180, 186, 194);
    }
    return palette;
}

INT_PTR AppController::HandleThemeColorMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    HDC hdc = reinterpret_cast<HDC>(wParam);
    HWND control = reinterpret_cast<HWND>(lParam);
    switch (message)
    {
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkColor(hdc, _palette.windowBackground);
        SetTextColor(hdc, _palette.textColor);
        return reinterpret_cast<INT_PTR>(_dialogBrush ? _dialogBrush : GetStockObject(WHITE_BRUSH));
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetBkColor(hdc, _palette.controlBackground);
        SetTextColor(hdc, _palette.textColor);
        return reinterpret_cast<INT_PTR>(_controlBrush ? _controlBrush : GetStockObject(WHITE_BRUSH));
    default:
        break;
    }
    return FALSE;
}

void AppController::ApplyFlatBorderToControls()
{
    if (_dialog == nullptr)
    {
        return;
    }
    const std::array<int, 8> borderControls{
        IDC_COMMAND_LIST,
        IDC_EDIT_LOG,
        IDC_EDIT_COMMAND,
        IDC_EDIT_SMS_NUMBER,
        IDC_EDIT_SMS_TEXT,
        IDC_COMBO_PORT,
        IDC_COMBO_BAUD,
        IDC_COMBO_THEME
    };
    for (int id : borderControls)
    {
        ApplyFlatBorderToControl(id);
    }
}

void AppController::ApplyFlatBorderToControl(int controlId)
{
    if (_dialog == nullptr)
    {
        return;
    }
    HWND control = GetDlgItem(_dialog, controlId);
    if (!control)
    {
        return;
    }
    ApplyFlatBorderToWindow(control, static_cast<UINT_PTR>(controlId));

    wchar_t className[32]{};
    if (GetClassNameW(control, className, static_cast<int>(std::size(className))) <= 0)
    {
        return;
    }
    if (_wcsicmp(className, L"ComboBox") != 0)
    {
        return;
    }

    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(control, &info))
    {
        if (info.hwndList)
        {
            ApplyFlatBorderToWindow(info.hwndList);
        }
        if (info.hwndItem)
        {
            ApplyFlatBorderToWindow(info.hwndItem);
        }
    }
}

void AppController::ApplyFlatBorderToWindow(HWND control, UINT_PTR subclassId)
{
    if (control == nullptr)
    {
        return;
    }
    if (subclassId == 0)
    {
        subclassId = reinterpret_cast<UINT_PTR>(control);
    }
    RemoveWindowSubclass(control, FlatBorderSubclassProc, subclassId);
    LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
    style &= ~WS_BORDER;
    SetWindowLongPtrW(control, GWL_STYLE, style);
    LONG_PTR exStyle = GetWindowLongPtrW(control, GWL_EXSTYLE);
    exStyle &= ~WS_EX_CLIENTEDGE;
    SetWindowLongPtrW(control, GWL_EXSTYLE, exStyle);
    SetWindowPos(control, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    SetWindowSubclass(control, FlatBorderSubclassProc, subclassId, reinterpret_cast<DWORD_PTR>(this));
    DrawFlatBorder(control);
}

void AppController::DrawFlatBorder(HWND control) const
{
    if (control == nullptr)
    {
        return;
    }
    HDC windowDc = GetWindowDC(control);
    if (!windowDc)
    {
        return;
    }
    RECT rect{};
    GetWindowRect(control, &rect);
    OffsetRect(&rect, -rect.left, -rect.top);
    const HBRUSH brush = CreateSolidBrush(_palette.borderColor);
    if (brush != nullptr)
    {
        FrameRect(windowDc, &rect, brush);
        DeleteObject(brush);
    }
    ReleaseDC(control, windowDc);
}

bool AppController::IsComboSubclassId(UINT_PTR subclassId) const
{
    return subclassId == IDC_COMBO_PORT || subclassId == IDC_COMBO_BAUD || subclassId == IDC_COMBO_THEME;
}

void AppController::PaintFlatCombo(HWND combo, HDC targetDc) const
{
    if (combo == nullptr || targetDc == nullptr)
    {
        return;
    }

    RECT client{};
    GetClientRect(combo, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0)
    {
        return;
    }

    HDC bufferDc = CreateCompatibleDC(targetDc);
    HBITMAP bufferBmp = CreateCompatibleBitmap(targetDc, width, height);
    HBITMAP oldBmp = nullptr;
    if (bufferDc == nullptr || bufferBmp == nullptr)
    {
        if (bufferDc)
        {
            DeleteDC(bufferDc);
        }
        if (bufferBmp)
        {
            DeleteObject(bufferBmp);
        }
        bufferDc = nullptr;
    }
    else
    {
        oldBmp = static_cast<HBITMAP>(SelectObject(bufferDc, bufferBmp));
    }
    HDC drawDc = bufferDc != nullptr ? bufferDc : targetDc;

    const bool enabled = IsWindowEnabled(combo) != FALSE;
    const bool hasFocus = GetFocus() == combo;
    const bool dropped = SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) != 0;
    COLORREF fillColor = _palette.controlBackground;
    if (hasFocus || dropped)
    {
        fillColor = (_themeMode == ThemeMode::Dark) ? RGB(55, 55, 55) : RGB(225, 235, 250);
    }
    const COLORREF textColor = enabled ? _palette.textColor : RGB(160, 160, 160);
    const COLORREF arrowColor = textColor;

    HBRUSH fillBrush = CreateSolidBrush(fillColor);
    FillRect(drawDc, &client, fillBrush);
    DeleteObject(fillBrush);

    SetBkMode(drawDc, TRANSPARENT);
    SetTextColor(drawDc, textColor);

    const int sysButtonWidth = GetSystemMetrics(SM_CXVSCROLL);
    const int buttonWidth = (sysButtonWidth > 16) ? sysButtonWidth : 16;
    RECT textRect = client;
    textRect.left += 6;
    const int candidateRight = textRect.right - buttonWidth - 4;
    textRect.right = (candidateRight > textRect.left) ? candidateRight : textRect.left;

    std::wstring text = GetComboDisplayText(combo);
    if (!text.empty())
    {
        DrawTextW(drawDc, text.c_str(), static_cast<int>(text.size()), &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    }

    RECT buttonRect = client;
    buttonRect.left = buttonRect.right - buttonWidth;
    HBRUSH buttonBrush = CreateSolidBrush((_themeMode == ThemeMode::Dark) ? RGB(70, 70, 70) : RGB(242, 244, 247));
    FillRect(drawDc, &buttonRect, buttonBrush);
    DeleteObject(buttonBrush);

    POINT arrow[3]{};
    const int centerX = (buttonRect.left + buttonRect.right) / 2;
    const int centerY = (buttonRect.top + buttonRect.bottom) / 2;
    const int size = 4;
    arrow[0] = {centerX - size, centerY - 1};
    arrow[1] = {centerX + size, centerY - 1};
    arrow[2] = {centerX, centerY + size};
    HPEN pen = CreatePen(PS_SOLID, 1, arrowColor);
    HBRUSH arrowBrush = CreateSolidBrush(arrowColor);
    const HPEN oldPen = static_cast<HPEN>(SelectObject(drawDc, pen));
    const HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(drawDc, arrowBrush));
    Polygon(drawDc, arrow, 3);
    SelectObject(drawDc, oldPen);
    SelectObject(drawDc, oldBrush);
    DeleteObject(pen);
    DeleteObject(arrowBrush);

    if (bufferDc != nullptr)
    {
        BitBlt(targetDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
        SelectObject(bufferDc, oldBmp);
        DeleteObject(bufferBmp);
        DeleteDC(bufferDc);
    }
}

std::wstring AppController::GetComboDisplayText(HWND combo) const
{
    int length = GetWindowTextLengthW(combo);
    if (length > 0)
    {
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(combo, text.data(), length + 1);
        text.resize(length);
        return text;
    }
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index >= 0)
    {
        const LRESULT lbLength = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
        if (lbLength > 0)
        {
            std::wstring text(static_cast<std::size_t>(lbLength) + 1, L'\0');
            SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text.data()));
            text.resize(lbLength);
            return text;
        }
    }
    return std::wstring();
}

LRESULT CALLBACK AppController::FlatBorderSubclassProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR reference)
{
    auto* controller = reinterpret_cast<AppController*>(reference);

    if (controller != nullptr && controller->IsComboSubclassId(subclassId))
    {
        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hWnd, &ps);
            controller->PaintFlatCombo(hWnd, dc);
            EndPaint(hWnd, &ps);
            controller->DrawFlatBorder(hWnd);
            return 0;
        }
        case WM_PRINTCLIENT:
            controller->PaintFlatCombo(hWnd, reinterpret_cast<HDC>(wParam));
            controller->DrawFlatBorder(hWnd);
            return 0;
        default:
            break;
        }
    }

    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hWnd, FlatBorderSubclassProc, subclassId);
        return DefSubclassProc(hWnd, message, wParam, lParam);
    }

    if (message == WM_NCPAINT)
    {
        if (controller != nullptr)
        {
            controller->DrawFlatBorder(hWnd);
        }
        return 0;
    }

    const LRESULT result = DefSubclassProc(hWnd, message, wParam, lParam);
    if (controller != nullptr && (message == WM_PAINT || message == WM_THEMECHANGED || message == WM_SETFOCUS || message == WM_KILLFOCUS))
    {
        controller->DrawFlatBorder(hWnd);
    }
    return result;
}

std::filesystem::path AppController::ResolveConfigPath() const
{
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(_instance, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    return exePath.parent_path() / L"commands.xml";
}

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prevInstance, _In_ PWSTR cmdLine, _In_ int showCmd)
{
    UNREFERENCED_PARAMETER(prevInstance);
    UNREFERENCED_PARAMETER(cmdLine);
    UNREFERENCED_PARAMETER(showCmd);

    AppController controller;
    if (!controller.Initialize(instance))
    {
        return -1;
    }
    return static_cast<int>(controller.Run());
}
