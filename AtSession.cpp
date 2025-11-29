/*------------------------------------------------------------------------
名称：AT 会话实现
说明：实现串口指令、日志与短信解析逻辑
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-29
备注：无
------------------------------------------------------------------------*/
#include "AtSession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <thread>
#include <windows.h>

namespace
{
    std::wstring Trim(const std::wstring& text)
    {
        std::size_t start = 0;
        std::size_t end = text.size();
        while (start < end && std::iswspace(static_cast<wint_t>(text[start])) != 0)
        {
            ++start;
        }
        while (end > start && std::iswspace(static_cast<wint_t>(text[end - 1])) != 0)
        {
            --end;
        }
        return text.substr(start, end - start);
    }
}

AtSession::AtSession()
    : _waitingSmsContent(false)
{
}

AtSession::~AtSession()
{
    Disconnect();
}

bool AtSession::Connect(const std::wstring& portName, unsigned long baudRate)
{
    Disconnect();
    _lineBuffer.clear();
    _waitingSmsContent = false;
    _pendingEchoes.clear();
    _port.SetDataHandler([this](const std::string& chunk)
    {
        HandleIncoming(chunk);
    });
    if (!_port.Open(portName, baudRate))
    {
        _port.SetDataHandler(nullptr);
        return false;
    }
    AppendLog(L"已连接 " + portName + L" 串口");
    ConfigureAfterConnect();
    return true;
}

void AtSession::Disconnect()
{
    if (_port.IsOpen())
    {
        _port.SetDataHandler(nullptr);
        _port.Close();
        AppendLog(L"串口已断开");
    }
    _pendingEchoes.clear();
}

bool AtSession::IsConnected() const noexcept
{
    return _port.IsOpen();
}

bool AtSession::SendCommand(const std::wstring& commandText)
{
    if (!IsConnected())
    {
        return false;
    }
    std::wstring trimmed = Trim(commandText);
    if (trimmed.empty())
    {
        return false;
    }
    std::wstring payload = trimmed;
    if (!payload.empty() && payload.back() != L'\r')
    {
        payload.push_back(L'\r');
    }
    const auto buffer = WideToUtf8(payload);
    if (buffer.empty())
    {
        return false;
    }
    if (_port.Write(buffer))
    {
        AppendLog(L"--> " + trimmed);
        _pendingEchoes.push_back(trimmed);
        if (_pendingEchoes.size() > 32)
        {
            _pendingEchoes.pop_front();
        }
        return true;
    }
    return false;
}

bool AtSession::SendSms(const std::wstring& smsContent)
{
    if (!IsConnected())
    {
        return false;
    }
    const std::wstring trimmed = Trim(smsContent);
    if (trimmed.empty())
    {
        return false;
    }
    if (_smsProfile.serviceCenter.empty() == false)
    {
        SendCommand(L"AT+CSCA=\"" + _smsProfile.serviceCenter + L"\"");
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    if (!SendCommand(L"AT+CMGF=1"))
    {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    if (_smsProfile.targetNumber.empty())
    {
        AppendLog(L"未配置短信目标号码");
        return false;
    }
    if (!SendCommand(L"AT+CMGS=\"" + _smsProfile.targetNumber + L"\""))
    {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto payload = WideToUtf8(trimmed);
    payload.push_back(static_cast<char>(0x1A));
    const bool ok = _port.Write(payload);
    if (ok)
    {
        AppendLog(L"已发送短信: " + trimmed);
    }
    return ok;
}

void AtSession::SetSmsProfile(const SmsProfile& profile)
{
    _smsProfile = profile;
}

void AtSession::SetLogCallback(LogCallback callback)
{
    std::lock_guard<std::mutex> guard(_callbackMutex);
    _logCallback = std::move(callback);
}

void AtSession::SetSmsCallback(SmsCallback callback)
{
    std::lock_guard<std::mutex> guard(_callbackMutex);
    _smsCallback = std::move(callback);
}

void AtSession::HandleIncoming(const std::string& chunk)
{
    _lineBuffer.append(chunk);
    const std::string delimiter = "\r\n";
    while (true)
    {
        const auto pos = _lineBuffer.find(delimiter);
        if (pos == std::string::npos)
        {
            break;
        }
        std::string line = _lineBuffer.substr(0, pos);
        _lineBuffer.erase(0, pos + delimiter.size());
        if (line.empty())
        {
            continue;
        }
        ProcessLine(Utf8ToWide(line));
    }
}

void AtSession::ProcessLine(const std::wstring& line)
{
    const std::wstring normalized = Trim(line);
    if (normalized.empty())
    {
        return;
    }
    if (!_pendingEchoes.empty() && normalized == _pendingEchoes.front())
    {
        _pendingEchoes.pop_front();
        return;
    }
    if (normalized.rfind(L"+CMT:", 0) == 0 || normalized.rfind(L"+CMGR:", 0) == 0)
    {
        _waitingSmsContent = true;
        _lastSmsHeader = normalized;
        return;
    }
    if (normalized.rfind(L"+CMTI:", 0) == 0)
    {
        HandleCmtiNotification(normalized);
        return;
    }
    if (_waitingSmsContent)
    {
        _waitingSmsContent = false;
        SmsCallback callbackCopy;
        {
            std::lock_guard<std::mutex> guard(_callbackMutex);
            callbackCopy = _smsCallback;
        }
        if (callbackCopy)
        {
            callbackCopy(_lastSmsHeader, line);
        }
        AppendLog(L"收到短信: " + line);
        return;
    }
    AppendLog(L"<-- " + normalized);
}

void AtSession::ConfigureAfterConnect()
{
    static const std::array<std::wstring, 3> commands{
        L"AT",
        L"AT+CMGF=1",
        L"AT+CNMI=2,1,0,0,0"
    };
    for (const auto& command : commands)
    {
        if (!SendCommand(command))
        {
            AppendLog(L"初始化指令发送失败: " + command);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
}

void AtSession::HandleCmtiNotification(const std::wstring& line)
{
    const auto comma = line.rfind(L',');
    if (comma == std::wstring::npos)
    {
        AppendLog(L"CMTI 通知格式异常: " + line);
        return;
    }
    std::wstring indexText = Trim(line.substr(comma + 1));
    if (indexText.empty())
    {
        AppendLog(L"CMTI 通知缺少索引: " + line);
        return;
    }
    AppendLog(L"检测到新短信，读取索引 " + indexText);
    if (!SendCommand(L"AT+CMGR=" + indexText))
    {
        AppendLog(L"自动读取短信失败: " + indexText);
    }
}

std::wstring AtSession::Utf8ToWide(const std::string& text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0)
    {
        return std::wstring();
    }
    std::wstring buffer(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), buffer.data(), needed);
    return buffer;
}

std::string AtSession::WideToUtf8(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
    {
        return std::string();
    }
    std::string buffer(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), buffer.data(), needed, nullptr, nullptr);
    return buffer;
}

void AtSession::AppendLog(const std::wstring& line)
{
    LogCallback callbackCopy;
    {
        std::lock_guard<std::mutex> guard(_callbackMutex);
        callbackCopy = _logCallback;
    }
    if (callbackCopy)
    {
        callbackCopy(line);
    }
}
