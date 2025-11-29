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
};

/// <summary>WinMain 入口。</summary>
int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prevInstance, _In_ PWSTR cmdLine, _In_ int showCmd);
