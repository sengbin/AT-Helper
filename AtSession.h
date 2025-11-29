/*------------------------------------------------------------------------
名称：AT 会话模块
说明：负责串口连接、指令发送与短信解析
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-29
备注：无
------------------------------------------------------------------------*/
#pragma once

#include "CommandConfig.h"
#include "SerialPort.h"

#include <functional>
#include <mutex>
#include <string>

/// <summary>封装 AT 会话逻辑。</summary>
class AtSession
{
public:
    using LogCallback = std::function<void(const std::wstring&)>;
    using SmsCallback = std::function<void(const std::wstring& header, const std::wstring& content)>;

    AtSession();
    ~AtSession();

    /// <summary>尝试连接指定串口。</summary>
    bool Connect(const std::wstring& portName, unsigned long baudRate);

    /// <summary>断开当前连接。</summary>
    void Disconnect();

    /// <summary>是否已经连接。</summary>
    bool IsConnected() const noexcept;

    /// <summary>发送一条 AT 指令。</summary>
    bool SendCommand(const std::wstring& commandText);

    /// <summary>发送短信内容。</summary>
    bool SendSms(const std::wstring& smsContent);

    /// <summary>配置短信参数。</summary>
    void SetSmsProfile(const SmsProfile& profile);

    /// <summary>注册日志回调。</summary>
    void SetLogCallback(LogCallback callback);

    /// <summary>注册短信接收回调。</summary>
    void SetSmsCallback(SmsCallback callback);

private:
    void AttachCallbacks();
    void HandleIncoming(const std::string& chunk);
    void ProcessLine(const std::wstring& line);
    static std::wstring Utf8ToWide(const std::string& text);
    static std::string WideToUtf8(const std::wstring& text);
    void AppendLog(const std::wstring& line);

private:
    SerialPort _port;
    SmsProfile _smsProfile;
    LogCallback _logCallback;
    SmsCallback _smsCallback;
    std::mutex _callbackMutex;
    std::string _lineBuffer;
    std::wstring _lastSmsHeader;
    bool _waitingSmsContent;
};
