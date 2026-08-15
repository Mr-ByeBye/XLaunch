#include "config/BackupManager.h"
#include "config/ConfigManager.h"
#include "localization/LanguageManager.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    bool Check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }
}

int main()
{
    const std::filesystem::path defaultPath = xlaunch::ConfigManager::DefaultConfigPath();
    const bool defaultPathIsPortable = defaultPath.filename() == L"xlaunch.json" &&
        defaultPath.parent_path().filename() == L"config";
    const xlaunch::AppConfig defaults = xlaunch::MakeDefaultConfig();
    const bool requestedDefaults =
        defaults.appearance.showBorders &&
        std::abs(defaults.appearance.horizontalSpacing - 4.0f) < 0.001f &&
        std::abs(defaults.appearance.verticalSpacing - 4.0f) < 0.001f &&
        defaults.appearance.categorySwitchMode == xlaunch::CategorySwitchMode::Hover &&
        defaults.appearance.categoryHoverDelayMs == 250 &&
        defaults.window.startupPosition == xlaunch::StartupPositionMode::Cursor &&
        defaults.window.temporaryPinKey == xlaunch::TemporaryPinKey::Control &&
        defaults.hotkey.enabled &&
        defaults.hotkey.trigger == xlaunch::HotkeyTrigger::Keyboard &&
        defaults.hotkey.modifiers == (xlaunch::HotkeyControl | xlaunch::HotkeyAlt) &&
        defaults.hotkey.virtualKey == 0x20 &&
        defaults.startupPriority == xlaunch::StartupPriority::Disabled &&
        !defaults.startWithWindows;
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("XLaunchConfigTest-" + std::to_string(suffix));
    const std::filesystem::path configPath = directory / "xlaunch.json";

    xlaunch::LanguageManager::Initialize(directory, "en");
    const bool builtInEnglishLoaded = std::string(xlaunch::LanguageManager::Get("常用")) == "Common" &&
        std::string(xlaunch::LanguageManager::Get("钉住")) == "Pin and keep visible";
    const std::filesystem::path languageDirectory = directory / "lang";
    bool languageFilesReleased = std::filesystem::exists(languageDirectory / "zh-cn.ini") &&
        std::filesystem::exists(languageDirectory / "zh-tw.ini") &&
        std::filesystem::exists(languageDirectory / "en.ini");
    std::ofstream(languageDirectory / "de.ini", std::ios::binary | std::ios::trunc)
        << "[strings]\n语言=Sprache\n关闭=Schliessen\n";
    const bool customLanguageLoaded = xlaunch::LanguageManager::SetLanguage("de") &&
        std::string(xlaunch::LanguageManager::Get("语言")) == "Sprache" &&
        std::string(xlaunch::LanguageManager::Get("未翻译")) == "未翻译";

    xlaunch::ConfigManager manager(configPath);
    xlaunch::AppConfig config = xlaunch::MakeDefaultConfig();
    config.categories[0].name = "常用工具";
    config.categories.push_back(xlaunch::Category{ xlaunch::MakeId("category"), "开发", {} });

    xlaunch::LaunchItem item;
    item.id = xlaunch::MakeId("item");
    item.target = "C:\\Windows\\notepad.exe";
    item.automaticName = xlaunch::DeriveAutomaticName(item.target);
    item.type = xlaunch::DetectItemType(item.target);
    item.sortOrder = 0;
    item.globalShortcut = { true, xlaunch::HotkeyControl | xlaunch::HotkeyAlt, 'N' };
    item.localShortcut = { true, xlaunch::HotkeyControl, '1' };
    config.categories[0].items.push_back(item);                         // 新增
    config.categories[0].items[0].customName = "文本编辑器";           // 编辑

    xlaunch::LaunchItem shortcutItem;
    shortcutItem.id = xlaunch::MakeId("item");
    shortcutItem.automaticName = "命令快捷方式";
    shortcutItem.target = "C:\\Windows\\System32\\cmd.exe";
    shortcutItem.arguments = "/c exit 0";
    shortcutItem.workingDirectory = "C:\\Windows\\System32";
    shortcutItem.type = xlaunch::ItemType::Shortcut;
    shortcutItem.sortOrder = 1;
    config.categories[0].items.push_back(shortcutItem);

    xlaunch::LaunchItem copy = config.categories[0].items[0];
    copy.id = xlaunch::MakeId("item");
    copy.customName = "文本编辑器副本";
    config.categories[0].items.push_back(copy);                         // 复制
    config.categories[1].items.push_back(std::move(config.categories[0].items[2]));
    config.categories[0].items.erase(config.categories[0].items.begin() + 2); // 移动
    config.categories[1].items.clear();                                 // 删除项目
    config.categories[1].name = "研发";                                // 重命名分类

    config.appearance.showNames = false;
    config.appearance.showBorders = true;
    config.appearance.iconSize = 64;
    config.appearance.horizontalSpacing = 18.0f;
    config.appearance.verticalSpacing = 20.0f;
    config.appearance.compactColumnMinimumWidth = 72.0f;
    config.appearance.windowOpacity = 0.72f;
    config.appearance.itemActivationMode = xlaunch::ItemActivationMode::DoubleClick;
    config.appearance.categoryBarLayout = xlaunch::CategoryBarLayout::Right;
    config.appearance.categorySidebarMaximumWidth = 96.0f;
    config.appearance.categoryBarTextDirection = xlaunch::CategoryBarTextDirection::Vertical;
    config.appearance.categoryBarVerticalReading = xlaunch::CategoryBarVerticalReading::BottomToTop;
    config.appearance.categorySwitchMode = xlaunch::CategorySwitchMode::Hover;
    config.appearance.categoryHoverDelayMs = 420;
    config.appearance.language = "en";
    config.window.startupPosition = xlaunch::StartupPositionMode::Custom;
    config.window.corner = xlaunch::ScreenCorner::BottomRight;
    config.window.temporaryPinKey = xlaunch::TemporaryPinKey::Shift;
    config.window.customX = 321;
    config.window.customY = 654;
    config.window.width = 888;
    config.window.height = 666;
    config.window.title = "我的启动器";
    config.window.centerTitle = true;
    config.window.keepVisible = true;
    config.hotkey.enabled = true;
    config.hotkey.trigger = xlaunch::HotkeyTrigger::Keyboard;
    config.hotkey.modifiers = xlaunch::HotkeyControl | xlaunch::HotkeyShift;
    config.hotkey.virtualKey = 'K';
    config.hotkey.mouseButton = xlaunch::MouseButton::X2;
    config.hotkey.heldMouseButton = xlaunch::MouseButton::Middle;
    config.hotkey.mouseDoubleClick = true;
    config.startWithWindows = true;
    config.startupPriority = xlaunch::StartupPriority::VeryHigh;
    config.backup.automatic = true;
    config.backup.keepCount = 7;

    std::string error;
    bool success = Check(defaultPathIsPortable, "默认配置未位于程序目录的 config 子目录");
    success &= Check(languageFilesReleased, "内置语言文件未自动释放");
    success &= Check(builtInEnglishLoaded, "内置英文翻译未正确加载");
    success &= Check(customLanguageLoaded, "自定义 INI 语言未被发现或回退失效");
    success &= Check(requestedDefaults, "首次运行默认设置不正确");
    success &= Check(manager.Save(config, error), error.c_str());
    const xlaunch::ConfigManager::LoadResult loaded = manager.Load();
    success &= Check(loaded.error.empty(), loaded.error.c_str());
    success &= Check(loaded.config.categories.size() == 2, "分类数量不正确");
    success &= Check(loaded.config.categories[0].name == "常用工具", "分类名称未保存");
    success &= Check(loaded.config.categories[1].name == "研发", "分类重命名未保存");
    success &= Check(loaded.config.categories[0].items.size() == 2, "项目增删结果不正确");
    success &= Check(loaded.config.categories[0].items[0].customName == "文本编辑器", "项目编辑未保存");
    success &= Check(loaded.config.categories[0].items[0].type == xlaunch::ItemType::Executable, "项目类型未恢复");
    success &= Check(loaded.config.categories[0].items[0].sortOrder == 0, "项目排序值未恢复");
    success &= Check(loaded.config.categories[0].items[0].globalShortcut.enabled &&
        loaded.config.categories[0].items[0].globalShortcut.virtualKey == 'N', "项目全局快捷键未恢复");
    success &= Check(loaded.config.categories[0].items[0].localShortcut.enabled &&
        loaded.config.categories[0].items[0].localShortcut.virtualKey == '1', "项目软件内快捷键未恢复");
    success &= Check(loaded.config.categories[0].items[1].type == xlaunch::ItemType::Shortcut, "lnk 类型未恢复");
    success &= Check(loaded.config.categories[0].items[1].arguments == "/c exit 0", "lnk 参数未恢复");
    success &= Check(loaded.config.categories[0].items[1].workingDirectory == "C:\\Windows\\System32", "lnk 工作目录未恢复");
    success &= Check(loaded.config.categories[1].items.empty(), "项目移动/删除结果不正确");
    success &= Check(!loaded.config.appearance.showNames, "名称设置未保存");
    success &= Check(loaded.config.appearance.showBorders, "边框设置未保存");
    success &= Check(loaded.config.appearance.iconSize == 64, "图标大小未保存");
    success &= Check(std::abs(loaded.config.appearance.compactColumnMinimumWidth - 72.0f) < 0.001f,
        "紧凑模式列最小宽度未保存");
    success &= Check(loaded.config.appearance.itemActivationMode == xlaunch::ItemActivationMode::DoubleClick,
        "项目启动方式未保存");
    success &= Check(loaded.config.appearance.categoryBarLayout == xlaunch::CategoryBarLayout::Right &&
        std::abs(loaded.config.appearance.categorySidebarMaximumWidth - 96.0f) < 0.001f &&
        loaded.config.appearance.categoryBarTextDirection == xlaunch::CategoryBarTextDirection::Vertical &&
        loaded.config.appearance.categoryBarVerticalReading == xlaunch::CategoryBarVerticalReading::BottomToTop,
        "分类栏布局设置未保存");
    success &= Check(std::abs(loaded.config.appearance.windowOpacity - 0.72f) < 0.001f, "窗口透明度未保存");
    success &= Check(loaded.config.appearance.categorySwitchMode == xlaunch::CategorySwitchMode::Hover &&
        loaded.config.appearance.categoryHoverDelayMs == 420, "分类悬停切换设置未保存");
    success &= Check(loaded.config.appearance.language == "en", "语言设置未保存");
    success &= Check(loaded.config.window.startupPosition == xlaunch::StartupPositionMode::Custom, "启动位置模式未保存");
    success &= Check(loaded.config.window.corner == xlaunch::ScreenCorner::BottomRight, "屏幕角落未保存");
    success &= Check(loaded.config.window.temporaryPinKey == xlaunch::TemporaryPinKey::Shift,
        "连续启动临时钉住按键未保存");
    success &= Check(loaded.config.window.customX == 321 && loaded.config.window.customY == 654, "自定义窗口位置未保存");
    success &= Check(loaded.config.window.width == 888 && loaded.config.window.height == 666, "窗口尺寸未保存");
    success &= Check(loaded.config.window.title == "我的启动器" && loaded.config.window.centerTitle && loaded.config.window.keepVisible,
        "自定义标题或固定显示状态未保存");
    success &= Check(loaded.config.hotkey.enabled, "快捷键启用状态未保存");
    success &= Check(loaded.config.hotkey.trigger == xlaunch::HotkeyTrigger::Keyboard, "快捷键类型未保存");
    success &= Check(loaded.config.hotkey.modifiers == (xlaunch::HotkeyControl | xlaunch::HotkeyShift) &&
        loaded.config.hotkey.virtualKey == 'K', "自定义快捷键未保存");
    success &= Check(loaded.config.hotkey.mouseButton == xlaunch::MouseButton::X2 &&
        loaded.config.hotkey.heldMouseButton == xlaunch::MouseButton::Middle && loaded.config.hotkey.mouseDoubleClick,
        "鼠标手势未保存");
    success &= Check(loaded.config.startWithWindows, "开机自启设置未保存");
    success &= Check(loaded.config.startupPriority == xlaunch::StartupPriority::VeryHigh,
        "自启优先级未保存");
    success &= Check(loaded.config.backup.automatic && loaded.config.backup.keepCount == 7, "自动备份设置未保存");
    success &= Check(!std::filesystem::exists(configPath.wstring() + L".tmp"), "临时配置文件未清理");

    const std::filesystem::path legacyConfigPath = directory / "legacy-realtime.json";
    std::ofstream(legacyConfigPath) << R"({"startupPriority":"realtime"})";
    const xlaunch::ConfigManager legacyManager(legacyConfigPath);
    const auto legacyConfig = legacyManager.Load();
    success &= Check(legacyConfig.error.empty() &&
        legacyConfig.config.startupPriority == xlaunch::StartupPriority::VeryHigh,
        "旧版 realtime 优先级未安全降级");
    success &= Check(!legacyConfig.config.startWithWindows &&
        legacyConfig.config.hotkey.trigger == xlaunch::HotkeyTrigger::Keyboard &&
        legacyConfig.config.hotkey.modifiers == (xlaunch::HotkeyControl | xlaunch::HotkeyAlt) &&
        legacyConfig.config.hotkey.virtualKey == 0x20,
        "缺省配置未使用安全的自启和键盘快捷键设置");

    error.clear();
    success &= Check(xlaunch::BackupManager::CreateAutomatic(configPath, 7, true, error), error.c_str());
    const std::filesystem::path exportPath = directory / "manual-export.json";
    success &= Check(xlaunch::BackupManager::Export(configPath, exportPath, error), error.c_str());
    success &= Check(std::filesystem::exists(exportPath), "手动导出文件不存在");
    const xlaunch::ConfigManager exportedManager(exportPath);
    const auto exportedConfig = exportedManager.Load();
    success &= Check(exportedConfig.error.empty() && exportedConfig.config.categories.size() == 2, "导出配置无法重新导入");
    bool foundAutomaticBackup = false;
    const std::filesystem::path backupDirectory = directory / "backups";
    for (const auto& entry : std::filesystem::directory_iterator(backupDirectory))
        foundAutomaticBackup |= entry.path().extension() == L".json";
    success &= Check(foundAutomaticBackup, "自动备份文件不存在");

    std::ofstream(configPath, std::ios::binary | std::ios::trunc) << "{ invalid json";
    const xlaunch::ConfigManager::LoadResult damaged = manager.Load();
    success &= Check(!damaged.error.empty(), "损坏配置未被识别");
    error.clear();
    success &= Check(manager.Save(damaged.config, error), error.c_str());
    std::error_code ignored;
    bool foundCorruptBackup = false;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.path().filename().wstring().find(L"xlaunch.json.corrupt-") == 0)
        {
            foundCorruptBackup = true;
            std::filesystem::remove(entry.path(), ignored);
        }
    }
    success &= Check(foundCorruptBackup, "损坏配置未在覆盖前备份");

    ignored.clear();
    std::filesystem::remove(configPath, ignored);
    std::filesystem::remove_all(directory, ignored);
    return success ? 0 : 1;
}
