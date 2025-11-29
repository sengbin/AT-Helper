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
#include <iterator>
#include <sstream>
#include <vector>
#include <Richedit.h>

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
    : _instance(nullptr), _dialog(nullptr), _richEditModule(nullptr)
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
}

bool AppController::Initialize(HINSTANCE instance)
{
    _instance = instance;
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
        return RGB(0, 120, 215);
    }
    if (text.rfind(L"<-- ", 0) == 0)
    {
        return RGB(0, 153, 0);
    }
    return GetSysColor(COLOR_WINDOWTEXT);
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
