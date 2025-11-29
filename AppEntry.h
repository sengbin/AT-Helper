/*------------------------------------------------------------------------
名称：应用入口头文件
说明：声明程序主控制器与窗口过程
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-29
备注：无
------------------------------------------------------------------------*/
#pragma once

#include "AtSession.h"
#include "CommandConfig.h"

#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

/// <summary>界面配色定义。</summary>
struct ThemePalette
{
    COLORREF windowBackground;
    COLORREF controlBackground;
    COLORREF textColor;
    COLORREF logBackground;
    COLORREF logTextColor;
    COLORREF sendTextColor;
    COLORREF receiveTextColor;
    COLORREF borderColor;
    COLORREF buttonBackground;
    COLORREF buttonHover;
    COLORREF buttonPressed;
    COLORREF buttonBorder;
    COLORREF buttonText;
    COLORREF buttonDisabled;
};

/// <summary>负责管理主对话框及业务逻辑。</summary>
class AppController
{
public:
    AppController();
    ~AppController();

    /// <summary>初始化控制器，例如加载配置。</summary>
    bool Initialize(HINSTANCE instance);

    /// <summary>对话框消息调度函数。</summary>
    static INT_PTR CALLBACK DialogRouter(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>运行对话框。</summary>
    INT_PTR Run();

private:
    INT_PTR OnInitDialog(HWND hWnd);
    INT_PTR HandleDialogMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    void HandleCommand(WPARAM wParam, LPARAM lParam);
    void HandleLogPayload(std::wstring* payload);
    void HandleSmsPayload(std::wstring* payload);
    void RefreshCommandList();
    void RefreshPortList();
    void AppendLog(const std::wstring& text);
    COLORREF ResolveLogColor(const std::wstring& text) const;
    void SetStatus(const std::wstring& text);
    bool TryConnectSelectedPort();
    void DisconnectPort();
    void SendCommandText(const std::wstring& text);
    void SendSelectedCommand();
    std::wstring GetSelectedPort() const;
    unsigned long GetSelectedBaud() const;
    void ReloadConfiguration();
    void ResetSessionCallbacks();
    std::filesystem::path ResolveConfigPath() const;
    /// <summary>初始化主题下拉框。</summary>
    void InitializeThemeSelector();
    /// <summary>响应主题下拉框切换。</summary>
    void OnThemeSelectionChanged();
    /// <summary>根据主题刷新配色。</summary>
    void ApplyTheme(ThemeMode mode);
    /// <summary>刷新主题下拉框的选中状态。</summary>
    void UpdateThemeComboSelection() const;
    /// <summary>重建用于着色的画刷。</summary>
    void RecreateThemeBrushes();
    /// <summary>构造指定主题的配色参数。</summary>
    ThemePalette BuildPalette(ThemeMode mode) const;
    /// <summary>处理 WM_CTLCOLOR 类消息。</summary>
    INT_PTR HandleThemeColorMessage(UINT message, WPARAM wParam, LPARAM lParam);
    /// <summary>批量应用线框风格。</summary>
    void ApplyFlatBorderToControls();
    /// <summary>为指定控件添加线框。</summary>
    void ApplyFlatBorderToControl(int controlId);
    /// <summary>为任意窗口句柄应用线框。</summary>
    void ApplyFlatBorderToWindow(HWND control, UINT_PTR subclassId = 0);
    /// <summary>绘制自定义线框。</summary>
    void DrawFlatBorder(HWND control) const;
    /// <summary>为顶部控件设置紧凑尺寸。</summary>
    void ApplyCompactControlMetrics();
    /// <summary>重设控件高度。</summary>
    void ResizeControlHeight(int controlId, int targetHeight);
    /// <summary>绘制主题按钮。</summary>
    bool DrawThemedButton(const DRAWITEMSTRUCT& dis) const;
    /// <summary>判断子类化 ID 是否对应下拉框。</summary>
    bool IsComboSubclassId(UINT_PTR subclassId) const;
    /// <summary>判断子类化 ID 是否对应日志控件。</summary>
    bool IsLogSubclassId(UINT_PTR subclassId) const;
    /// <summary>绘制扁平风格下拉框。</summary>
    void PaintFlatCombo(HWND combo, HDC targetDc) const;
    /// <summary>获取下拉框当前文本。</summary>
    std::wstring GetComboDisplayText(HWND combo) const;
    /// <summary>填充日志控件背景。</summary>
    void FillLogBackground(HWND logWindow, HDC targetDc) const;
    /// <summary>按需构造紧凑字体。</summary>
    HFONT ResolveCompactFont();
    static LRESULT CALLBACK FlatBorderSubclassProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR reference);

private:
    HINSTANCE _instance;
    HWND _dialog;
    std::filesystem::path _configPath;
    CommandConfig _config;
    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;
    std::vector<CommandItem> _commands;
    SmsProfile _smsProfile;
    AtSession _session;
    HMODULE _richEditModule;
    ThemeMode _themeMode;
    ThemePalette _palette;
    HBRUSH _dialogBrush;
    HBRUSH _controlBrush;
    HBRUSH _logBrush;
    HFONT _compactFont;
};

/// <summary>WinMain 入口。</summary>
int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prevInstance, _In_ PWSTR cmdLine, _In_ int showCmd);
