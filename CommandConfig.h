/*------------------------------------------------------------------------
名称：指令配置模块
说明：提供 AT 指令及短信配置的加载与保存能力
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-29
备注：无
------------------------------------------------------------------------*/
#pragma once

#include <filesystem>
#include <string>
#include <vector>

/// <summary>封装一条 AT 指令与简述。</summary>
struct CommandItem
{
    std::wstring text;
    std::wstring summary;
};

/// <summary>短信目标号码等配置。</summary>
struct SmsProfile
{
    std::wstring targetNumber;
    std::wstring serviceCenter;
};

/// <summary>界面主题选项。</summary>
enum class ThemeMode
{
    Light,
    Dark
};

/// <summary>负责读取与写入指令配置文件。</summary>
class CommandConfig
{
public:
    CommandConfig();

    /// <summary>加载指定路径的配置，若不存在则写入默认内容。</summary>
    bool Load(const std::filesystem::path& filePath);

    /// <summary>将当前设置写回文件。</summary>
    bool Save(const std::filesystem::path& filePath) const;

    /// <summary>获取指令集合。</summary>
    const std::vector<CommandItem>& GetCommands() const noexcept;

    /// <summary>获取短信配置。</summary>
    const SmsProfile& GetSmsProfile() const noexcept;

    /// <summary>设置指令集合。</summary>
    void SetCommands(std::vector<CommandItem> commands);

    /// <summary>设置短信配置。</summary>
    void SetSmsProfile(const SmsProfile& profile);

    /// <summary>获取主题设置。</summary>
    ThemeMode GetTheme() const noexcept;

    /// <summary>设置主题。</summary>
    void SetTheme(ThemeMode mode) noexcept;

private:
    void EnsureDefaults();
    bool Parse(const std::wstring& xmlText);
    std::wstring Serialize() const;
    static std::wstring EscapeXml(const std::wstring& value);

private:
    std::vector<CommandItem> _commands;
    SmsProfile _smsProfile;
    ThemeMode _theme;
};
