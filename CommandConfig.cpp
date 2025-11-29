/*------------------------------------------------------------------------
名称：指令配置实现
说明：实现 XML 指令配置的解析与写入逻辑
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-29
备注：无
------------------------------------------------------------------------*/
#include "CommandConfig.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <cwctype>
#include <windows.h>

namespace
{
    std::wstring Trim(const std::wstring& value)
    {
        const auto size = value.size();
        std::size_t start = 0;
        while(start < size && std::iswspace(static_cast<wint_t>(value[start])) != 0) {
            ++start;
        }
        std::size_t end = size;
        while(end > start && std::iswspace(static_cast<wint_t>(value[end - 1])) != 0) {
            --end;
        }
        return value.substr(start, end - start);
    }

    std::wstring Utf8ToWide(const std::string& text)
    {
        if(text.empty()) {
            return std::wstring();
        }
        const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if(needed <= 0) {
            return std::wstring();
        }
        std::wstring buffer(static_cast<std::size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), buffer.data(), needed);
        return buffer;
    }

    std::string WideToUtf8(const std::wstring& text)
    {
        if(text.empty()) {
            return std::string();
        }
        const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if(needed <= 0) {
            return std::string();
        }
        std::string buffer(static_cast<std::size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), buffer.data(), needed, nullptr, nullptr);
        return buffer;
    }

    bool ExtractAttribute(const std::wstring& node, const std::wstring& attribute, std::wstring& value)
    {
        const std::wstring token = attribute + L"=\"";
        const auto start = node.find(token);
        if(start == std::wstring::npos) {
            return false;
        }
        const auto valueStart = start + token.size();
        const auto end = node.find(L"\"", valueStart);
        if(end == std::wstring::npos) {
            return false;
        }
        value = node.substr(valueStart, end - valueStart);
        return true;
    }

    std::wstring UnescapeXml(const std::wstring& value)
    {
        std::wstring result;
        result.reserve(value.size());
        for(std::size_t i = 0; i < value.size(); ++i) {
            if(value[i] == L'&') {
                if(value.compare(i, 5, L"&amp;") == 0) {
                    result.push_back(L'&');
                    i += 4;
                } else if(value.compare(i, 4, L"&lt;") == 0) {
                    result.push_back(L'<');
                    i += 3;
                } else if(value.compare(i, 4, L"&gt;") == 0) {
                    result.push_back(L'>');
                    i += 3;
                } else if(value.compare(i, 6, L"&quot;") == 0) {
                    result.push_back(L'\"');
                    i += 5;
                } else if(value.compare(i, 5, L"&apos;") == 0) {
                    result.push_back(L'\'');
                    i += 4;
                } else {
                    result.push_back(value[i]);
                }
            } else {
                result.push_back(value[i]);
            }
        }
        return result;
    }
}

CommandConfig::CommandConfig()
{
    EnsureDefaults();
}

bool CommandConfig::Load(const std::filesystem::path& filePath)
{
    EnsureDefaults();
    std::error_code status;
    if(!std::filesystem::exists(filePath, status)) {
        return Save(filePath);
    }

    std::ifstream input(filePath, std::ios::binary);
    if(!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string raw = buffer.str();
    if(raw.empty()) {
        EnsureDefaults();
        return Save(filePath);
    }

    if(!Parse(Utf8ToWide(raw))) {
        EnsureDefaults();
        return Save(filePath);
    }
    return true;
}

bool CommandConfig::Save(const std::filesystem::path& filePath) const
{
    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    if(!output) {
        return false;
    }
    const auto xml = Serialize();
    output << WideToUtf8(xml);
    return true;
}

const std::vector<CommandItem>& CommandConfig::GetCommands() const noexcept
{
    return _commands;
}

const SmsProfile& CommandConfig::GetSmsProfile() const noexcept
{
    return _smsProfile;
}

ThemeMode CommandConfig::GetTheme() const noexcept
{
    return _theme;
}

void CommandConfig::SetCommands(std::vector<CommandItem> commands)
{
    _commands = std::move(commands);
}

void CommandConfig::SetSmsProfile(const SmsProfile& profile)
{
    _smsProfile = profile;
}

void CommandConfig::SetTheme(ThemeMode mode) noexcept
{
    _theme = mode;
}

void CommandConfig::EnsureDefaults()
{
    _commands = {
        {L"AT", L"模块握手"},
        {L"AT+CSQ", L"查询信号质量"},
        {L"AT+CREG?", L"查询网络注册"},
        {L"",L""},
        {L"AT+CMGF=1", L"设置短信文本模式"},
        {L"AT+CSCA?", L"查询短信服务中心号码"},
        {L"AT+CMGL=\"REC UNREAD\"", L"读取未读短信"},
        {L"AT+CMGL = \"ALL\"",L"读取所有短信" },
        {L"",L""},
        {L"AT&F", L"模块出厂化" },
        {L"AT+CFUN=1,1", L"重启模块" },
    };
    _smsProfile.targetNumber.clear();
    _smsProfile.serviceCenter.clear();
    _theme = ThemeMode::Light;
}

bool CommandConfig::Parse(const std::wstring& xmlText)
{
    std::vector<CommandItem> parsedCommands;
    SmsProfile parsedProfile = _smsProfile;
    ThemeMode parsedTheme = _theme;

    const auto settingsPos = xmlText.find(L"<settings");
    if(settingsPos != std::wstring::npos) {
        const auto closePos = xmlText.find(L"/>", settingsPos);
        if(closePos != std::wstring::npos) {
            const auto node = xmlText.substr(settingsPos, closePos - settingsPos + 2);
            std::wstring target;
            if(ExtractAttribute(node, L"smsTarget", target) && !target.empty()) {
                parsedProfile.targetNumber = UnescapeXml(target);
            }
            std::wstring service;
            if(ExtractAttribute(node, L"serviceCenter", service) && !service.empty()) {
                parsedProfile.serviceCenter = UnescapeXml(service);
            }
            std::wstring themeAttr;
            if(ExtractAttribute(node, L"theme", themeAttr) && !themeAttr.empty()) {
                std::wstring lowered = themeAttr;
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                {
                    return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
                });
                if(lowered == L"dark") {
                    parsedTheme = ThemeMode::Dark;
                } else {
                    parsedTheme = ThemeMode::Light;
                }
            }
        }
    }

    std::size_t search = 0;
    while(true) {
        const auto start = xmlText.find(L"<command", search);
        if(start == std::wstring::npos) {
            break;
        }
        auto end = xmlText.find(L"/>", start);
        if(end == std::wstring::npos) {
            break;
        }
        const auto node = xmlText.substr(start, end - start + 2);
        search = end + 2;

        std::wstring textAttr;
        if(!ExtractAttribute(node, L"text", textAttr)) {
            continue;
        }
        std::wstring summaryAttr;
        ExtractAttribute(node, L"summary", summaryAttr);
        CommandItem item{};
        item.text = UnescapeXml(textAttr);
        item.summary = UnescapeXml(summaryAttr);
        parsedCommands.push_back(std::move(item));
    }

    if(!parsedCommands.empty()) {
        _commands = std::move(parsedCommands);
    }
    _smsProfile = parsedProfile;
    _theme = parsedTheme;
    return true;
}

std::wstring CommandConfig::Serialize() const
{
    std::wostringstream stream;
    stream << L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    stream << L"<atHelper>\n";
    stream << L"  <settings";
    stream << L" theme=\"" << (_theme == ThemeMode::Dark ? L"dark" : L"light") << L"\"";
    if(!_smsProfile.targetNumber.empty()) {
        stream << L" smsTarget=\"" << EscapeXml(_smsProfile.targetNumber) << L"\"";
    }
    if(!_smsProfile.serviceCenter.empty()) {
        stream << L" serviceCenter=\"" << EscapeXml(_smsProfile.serviceCenter) << L"\"";
    }
    stream << L" />\n";
    stream << L"  <commands>\n";
    for(const auto& cmd : _commands) {
        stream << L"    <command text=\"" << EscapeXml(cmd.text)
            << L"\" summary=\"" << EscapeXml(cmd.summary) << L"\" />\n";
    }
    stream << L"  </commands>\n";
    stream << L"</atHelper>\n";
    return stream.str();
}

std::wstring CommandConfig::EscapeXml(const std::wstring& value)
{
    std::wstring escaped;
    escaped.reserve(value.size());
    for(const auto ch : value) {
        switch(ch) {
            case L'&':
                escaped.append(L"&amp;");
                break;
            case L'\"':
                escaped.append(L"&quot;");
                break;
            case L'\'':
                escaped.append(L"&apos;");
                break;
            case L'<':
                escaped.append(L"&lt;");
                break;
            case L'>':
                escaped.append(L"&gt;");
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}
