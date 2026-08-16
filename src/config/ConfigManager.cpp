#include "config/ConfigManager.h"

#include <fstream>
#include <chrono>
#include <windows.h>

#include "json.hpp"

namespace xlaunch
{
    using nlohmann::json;

    namespace
    {
        std::string ReadString(const json& value, const char* key)
        {
            const auto iterator = value.find(key);
            return iterator != value.end() && iterator->is_string() ? iterator->get<std::string>() : std::string{};
        }

        std::string PathToUtf8(const std::filesystem::path& path)
        {
            const auto value = path.u8string();
            return std::string(value.begin(), value.end());
        }

        std::filesystem::path ExecutableDirectory()
        {
            std::wstring executablePath(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
            if (length == 0 || length >= executablePath.size())
                return std::filesystem::current_path();
            executablePath.resize(length);
            return std::filesystem::path(executablePath).parent_path();
        }

        ItemType ReadItemType(const json& value, const std::string& target)
        {
            const std::string type = ReadString(value, "type");
            if (type == "exe") return ItemType::Executable;
            if (type == "folder") return ItemType::Folder;
            if (type == "url") return ItemType::Url;
            if (type == "lnk") return ItemType::Shortcut;
            if (type == "shell") return ItemType::Shell;
            if (type == "file") return ItemType::File;
            return DetectItemType(target);
        }

        StartupPositionMode ReadStartupPosition(const std::string& value)
        {
            if (value == "center") return StartupPositionMode::Center;
            if (value == "corner") return StartupPositionMode::Corner;
            if (value == "custom") return StartupPositionMode::Custom;
            if (value == "cursor") return StartupPositionMode::Cursor;
            return StartupPositionMode::Center;
        }

        ScreenCorner ReadScreenCorner(const std::string& value)
        {
            if (value == "topLeft") return ScreenCorner::TopLeft;
            if (value == "bottomLeft") return ScreenCorner::BottomLeft;
            if (value == "bottomRight") return ScreenCorner::BottomRight;
            return ScreenCorner::TopRight;
        }

        const char* StartupPositionName(StartupPositionMode mode)
        {
            switch (mode)
            {
            case StartupPositionMode::Center: return "center";
            case StartupPositionMode::Corner: return "corner";
            case StartupPositionMode::Custom: return "custom";
            case StartupPositionMode::Cursor: return "cursor";
            default: return "center";
            }
        }

        const char* ScreenCornerName(ScreenCorner corner)
        {
            switch (corner)
            {
            case ScreenCorner::TopLeft: return "topLeft";
            case ScreenCorner::BottomLeft: return "bottomLeft";
            case ScreenCorner::BottomRight: return "bottomRight";
            default: return "topRight";
            }
        }

        TemporaryPinKey ReadTemporaryPinKey(const std::string& value)
        {
            if (value == "shift") return TemporaryPinKey::Shift;
            if (value == "alt") return TemporaryPinKey::Alt;
            if (value == "win") return TemporaryPinKey::Win;
            return TemporaryPinKey::Control;
        }

        const char* TemporaryPinKeyName(TemporaryPinKey key)
        {
            switch (key)
            {
            case TemporaryPinKey::Shift: return "shift";
            case TemporaryPinKey::Alt: return "alt";
            case TemporaryPinKey::Win: return "win";
            default: return "ctrl";
            }
        }

        MouseButton ReadMouseButton(const std::string& value)
        {
            if (value == "middle") return MouseButton::Middle;
            if (value == "x1") return MouseButton::X1;
            if (value == "x2") return MouseButton::X2;
            return MouseButton::None;
        }

        const char* MouseButtonName(MouseButton value)
        {
            switch (value) { case MouseButton::Middle: return "middle"; case MouseButton::X1: return "x1"; case MouseButton::X2: return "x2"; default: return "none"; }
        }

        LaunchItem ReadItem(const json& value)
        {
            LaunchItem item;
            item.id = ReadString(value, "id");
            item.automaticName = ReadString(value, "automaticName");
            item.customName = ReadString(value, "customName");
            item.target = ReadString(value, "target");
            item.arguments = ReadString(value, "arguments");
            item.workingDirectory = ReadString(value, "workingDirectory");
            item.customIconPath = ReadString(value, "customIconPath");
            item.runAsAdministrator = value.value("runAsAdministrator", false);
            item.type = ReadItemType(value, item.target);
            item.sortOrder = value.value("sortOrder", 0);
            if (const auto shortcut = value.find("globalShortcut"); shortcut != value.end() && shortcut->is_object())
            {
                item.globalShortcut.enabled = shortcut->value("enabled", false);
                item.globalShortcut.modifiers = shortcut->value("modifiers", 0);
                item.globalShortcut.virtualKey = shortcut->value("virtualKey", 0);
            }
            if (const auto shortcut = value.find("localShortcut"); shortcut != value.end() && shortcut->is_object())
            {
                item.localShortcut.enabled = shortcut->value("enabled", false);
                item.localShortcut.modifiers = shortcut->value("modifiers", 0);
                item.localShortcut.virtualKey = shortcut->value("virtualKey", 0);
            }
            return item;
        }

        json WriteItem(const LaunchItem& item)
        {
            return {
                { "id", item.id },
                { "automaticName", item.automaticName },
                { "customName", item.customName },
                { "target", item.target },
                { "arguments", item.arguments },
                { "workingDirectory", item.workingDirectory },
                { "customIconPath", item.customIconPath },
                { "runAsAdministrator", item.runAsAdministrator },
                { "type", ItemTypeName(item.type) },
                { "sortOrder", item.sortOrder },
                { "globalShortcut", {
                    { "enabled", item.globalShortcut.enabled },
                    { "modifiers", item.globalShortcut.modifiers },
                    { "virtualKey", item.globalShortcut.virtualKey }
                } },
                { "localShortcut", {
                    { "enabled", item.localShortcut.enabled },
                    { "modifiers", item.localShortcut.modifiers },
                    { "virtualKey", item.localShortcut.virtualKey }
                } }
            };
        }

        std::string WindowsErrorMessage(DWORD code)
        {
            wchar_t* message = nullptr;
            const DWORD length = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                code,
                0,
                reinterpret_cast<wchar_t*>(&message),
                0,
                nullptr);
            std::string result = "Windows error " + std::to_string(code);
            if (length != 0 && message != nullptr)
            {
                const int bytes = WideCharToMultiByte(CP_UTF8, 0, message, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
                result.resize(bytes);
                WideCharToMultiByte(CP_UTF8, 0, message, static_cast<int>(length), result.data(), bytes, nullptr, nullptr);
                while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
                    result.pop_back();
            }
            if (message != nullptr)
                LocalFree(message);
            return result;
        }
    }

    ConfigManager::ConfigManager(std::filesystem::path configPath)
        : configPath_(std::move(configPath))
    {
    }

    std::filesystem::path ConfigManager::DefaultConfigPath()
    {
        return ExecutableDirectory() / L"config" / L"xlaunch.json";
    }

    ConfigManager::LoadResult ConfigManager::Load() const
    {
        sourceWasCorrupt_ = false;
        LoadResult result{ MakeDefaultConfig(), {} };
        if (!std::filesystem::exists(configPath_))
            return result;

        try
        {
            std::ifstream input(configPath_, std::ios::binary);
            if (!input)
            {
                result.error = "无法打开配置文件：" + PathToUtf8(configPath_);
                return result;
            }

            const json root = json::parse(input);
            AppConfig config;
            config.version = root.value("version", 1);
            config.releaseVersion = root.value("releaseVersion", "");
            if (const auto appearance = root.find("appearance"); appearance != root.end() && appearance->is_object())
            {
                config.appearance.showNames = appearance->value("showNames", true);
                config.appearance.showBorders = appearance->value("showBorders", true);
                config.appearance.iconSize = appearance->value("iconSize", 48);
                config.appearance.horizontalSpacing = appearance->value("horizontalSpacing", 4.0f);
                config.appearance.verticalSpacing = appearance->value("verticalSpacing", 4.0f);
                config.appearance.compactColumnMinimumWidth = appearance->value("compactColumnMinimumWidth", 180.0f);
                config.appearance.windowOpacity = appearance->value("windowOpacity", 1.0f);
                config.appearance.itemActivationMode = appearance->value("itemActivationMode", "singleClick") == "doubleClick"
                    ? ItemActivationMode::DoubleClick : ItemActivationMode::SingleClick;
                config.appearance.categorySwitchMode = appearance->value("categorySwitchMode", "hover") == "hover"
                    ? CategorySwitchMode::Hover : CategorySwitchMode::Click;
                config.appearance.categoryHoverDelayMs = appearance->value("categoryHoverDelayMs", 250);
                const std::string categoryBarLayout = appearance->value("categoryBarLayout", "topSingleLine");
                if (categoryBarLayout == "topWrap") config.appearance.categoryBarLayout = CategoryBarLayout::TopWrap;
                else if (categoryBarLayout == "left") config.appearance.categoryBarLayout = CategoryBarLayout::Left;
                else if (categoryBarLayout == "right") config.appearance.categoryBarLayout = CategoryBarLayout::Right;
                else config.appearance.categoryBarLayout = CategoryBarLayout::TopSingleLine;
                config.appearance.categorySidebarMaximumWidth = appearance->value("categorySidebarMaximumWidth", 80.0f);
                config.appearance.categoryBarTextDirection = appearance->value("categoryBarTextDirection", "horizontal") == "vertical"
                    ? CategoryBarTextDirection::Vertical : CategoryBarTextDirection::Horizontal;
                config.appearance.categoryBarVerticalReading = appearance->value("categoryBarVerticalReading", "topToBottom") == "bottomToTop"
                    ? CategoryBarVerticalReading::BottomToTop : CategoryBarVerticalReading::TopToBottom;
                config.appearance.language = appearance->value("language", "zh-cn");
            }

            if (const auto window = root.find("window"); window != root.end() && window->is_object())
            {
                config.window.title = window->value("title", "XLaunch");
                config.window.centerTitle = window->value("centerTitle", true);
                config.window.keepVisible = window->value("keepVisible", false);
                config.window.startupPosition = ReadStartupPosition(window->value("startupPosition", "cursor"));
                config.window.corner = ReadScreenCorner(window->value("corner", "topRight"));
                config.window.temporaryPinKey = ReadTemporaryPinKey(window->value("temporaryPinKey", "ctrl"));
                config.window.customX = window->value("customX", 100);
                config.window.customY = window->value("customY", 100);
                config.window.width = window->value("width", 760);
                config.window.height = window->value("height", 500);
            }

            if (const auto hotkey = root.find("hotkey"); hotkey != root.end() && hotkey->is_object())
            {
                const std::string trigger = hotkey->value("trigger", "keyboard");
                if (hotkey->contains("keyboardEnabled") || hotkey->contains("mouseEnabled"))
                {
                    config.hotkey.keyboardEnabled = hotkey->value("keyboardEnabled", true);
                    config.hotkey.mouseEnabled = hotkey->value("mouseEnabled", false);
                }
                else
                {
                    const bool enabled = hotkey->value("enabled", true);
                    config.hotkey.keyboardEnabled = enabled && trigger != "mouseGesture";
                    config.hotkey.mouseEnabled = enabled && trigger == "mouseGesture";
                }
                config.hotkey.modifiers = hotkey->value("modifiers", HotkeyControl | HotkeyAlt);
                config.hotkey.virtualKey = hotkey->value("virtualKey", 0x20);
                config.hotkey.mouseButton = ReadMouseButton(hotkey->value("mouseButton", "middle"));
                config.hotkey.heldMouseButton = ReadMouseButton(hotkey->value("heldMouseButton", "none"));
                config.hotkey.mouseDoubleClick = hotkey->value("mouseDoubleClick", false);
                if (trigger == "ctrlAltSpace") config.hotkey.modifiers = HotkeyControl | HotkeyAlt;
                else if (trigger == "ctrlShiftSpace") config.hotkey.modifiers = HotkeyControl | HotkeyShift;
                else if (trigger == "altSpace") config.hotkey.modifiers = HotkeyAlt;
            }
            config.startWithWindows = root.value("startWithWindows", false);
            const std::string startupPriority = root.value("startupPriority", "disabled");
            config.startupPriority = (startupPriority == "veryHigh" || startupPriority == "realtime")
                ? StartupPriority::VeryHigh
                : startupPriority == "high" ? StartupPriority::High : StartupPriority::Disabled;
            if (const auto backup = root.find("backup"); backup != root.end() && backup->is_object())
            {
                config.backup.automatic = backup->value("automatic", true);
                config.backup.keepCount = backup->value("keepCount", 10);
            }

            if (const auto categories = root.find("categories"); categories != root.end() && categories->is_array())
            {
                for (const json& categoryValue : *categories)
                {
                    if (!categoryValue.is_object())
                        continue;
                    Category category;
                    category.id = ReadString(categoryValue, "id");
                    category.name = ReadString(categoryValue, "name");
                    category.displayMode = categoryValue.value("displayMode", "iconGrid") == "compactList"
                        ? CategoryDisplayMode::CompactList : CategoryDisplayMode::IconGrid;
                    if (const auto items = categoryValue.find("items"); items != categoryValue.end() && items->is_array())
                    {
                        for (const json& itemValue : *items)
                        {
                            if (itemValue.is_object())
                                category.items.push_back(ReadItem(itemValue));
                        }
                    }
                    config.categories.push_back(std::move(category));
                }
            }

            NormalizeConfig(config);
            result.config = std::move(config);
        }
        catch (const std::exception& exception)
        {
            sourceWasCorrupt_ = true;
            result.error = "配置文件解析失败：" + std::string(exception.what());
        }
        return result;
    }

    bool ConfigManager::Save(const AppConfig& config, std::string& error) const
    {
        try
        {
            if (sourceWasCorrupt_ && std::filesystem::exists(configPath_))
            {
                const auto suffix = std::chrono::system_clock::now().time_since_epoch().count();
                const std::filesystem::path backupPath = configPath_.wstring() + L".corrupt-" + std::to_wstring(suffix);
                std::error_code backupError;
                std::filesystem::copy_file(configPath_, backupPath, std::filesystem::copy_options::none, backupError);
                if (backupError)
                {
                    error = "原配置已损坏，且无法创建备份，因此拒绝覆盖：" + backupError.message();
                    return false;
                }
                sourceWasCorrupt_ = false;
            }

            std::error_code directoryError;
            std::filesystem::create_directories(configPath_.parent_path(), directoryError);
            if (directoryError)
            {
                error = "无法创建配置目录：" + directoryError.message();
                return false;
            }

            json root;
            root["version"] = config.version;
            root["releaseVersion"] = config.releaseVersion;
            root["appearance"] = {
                { "showNames", config.appearance.showNames },
                { "showBorders", config.appearance.showBorders },
                { "iconSize", config.appearance.iconSize },
                { "horizontalSpacing", config.appearance.horizontalSpacing },
                { "verticalSpacing", config.appearance.verticalSpacing },
                { "compactColumnMinimumWidth", config.appearance.compactColumnMinimumWidth },
                { "windowOpacity", config.appearance.windowOpacity },
                { "itemActivationMode", config.appearance.itemActivationMode == ItemActivationMode::DoubleClick ? "doubleClick" : "singleClick" },
                { "categorySwitchMode", config.appearance.categorySwitchMode == CategorySwitchMode::Hover ? "hover" : "click" },
                { "categoryHoverDelayMs", config.appearance.categoryHoverDelayMs }
                ,{ "categoryBarLayout", config.appearance.categoryBarLayout == CategoryBarLayout::TopWrap ? "topWrap" :
                    config.appearance.categoryBarLayout == CategoryBarLayout::Left ? "left" :
                    config.appearance.categoryBarLayout == CategoryBarLayout::Right ? "right" : "topSingleLine" }
                ,{ "categorySidebarMaximumWidth", config.appearance.categorySidebarMaximumWidth }
                ,{ "categoryBarTextDirection", config.appearance.categoryBarTextDirection == CategoryBarTextDirection::Vertical ? "vertical" : "horizontal" }
                ,{ "categoryBarVerticalReading", config.appearance.categoryBarVerticalReading == CategoryBarVerticalReading::BottomToTop ? "bottomToTop" : "topToBottom" }
                ,{ "language", config.appearance.language }
            };
            root["window"] = {
                { "title", config.window.title },
                { "centerTitle", config.window.centerTitle },
                { "keepVisible", config.window.keepVisible },
                { "startupPosition", StartupPositionName(config.window.startupPosition) },
                { "corner", ScreenCornerName(config.window.corner) },
                { "temporaryPinKey", TemporaryPinKeyName(config.window.temporaryPinKey) },
                { "customX", config.window.customX },
                { "customY", config.window.customY },
                { "width", config.window.width },
                { "height", config.window.height }
            };
            root["hotkey"] = {
                { "keyboardEnabled", config.hotkey.keyboardEnabled },
                { "mouseEnabled", config.hotkey.mouseEnabled },
                { "modifiers", config.hotkey.modifiers },
                { "virtualKey", config.hotkey.virtualKey },
                { "mouseButton", MouseButtonName(config.hotkey.mouseButton) },
                { "heldMouseButton", MouseButtonName(config.hotkey.heldMouseButton) },
                { "mouseDoubleClick", config.hotkey.mouseDoubleClick }
            };
            root["startWithWindows"] = config.startWithWindows;
            root["startupPriority"] = config.startupPriority == StartupPriority::VeryHigh ? "veryHigh" :
                config.startupPriority == StartupPriority::High ? "high" : "disabled";
            root["backup"] = {
                { "automatic", config.backup.automatic },
                { "keepCount", config.backup.keepCount }
            };
            root["categories"] = json::array();
            for (const Category& category : config.categories)
            {
                json categoryValue{
                    { "id", category.id },
                    { "name", category.name },
                    { "displayMode", category.displayMode == CategoryDisplayMode::CompactList ? "compactList" : "iconGrid" },
                    { "items", json::array() }
                };
                for (const LaunchItem& item : category.items)
                    categoryValue["items"].push_back(WriteItem(item));
                root["categories"].push_back(std::move(categoryValue));
            }

            const std::filesystem::path temporaryPath = configPath_.wstring() + L".tmp";
            {
                std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
                if (!output)
                {
                    error = "无法创建临时配置文件：" + PathToUtf8(temporaryPath);
                    return false;
                }
                output << root.dump(2) << '\n';
                output.flush();
                if (!output)
                {
                    error = "写入临时配置文件失败。";
                    return false;
                }
            }

            if (!MoveFileExW(
                temporaryPath.c_str(),
                configPath_.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                error = "替换配置文件失败：" + WindowsErrorMessage(GetLastError());
                std::error_code ignored;
                std::filesystem::remove(temporaryPath, ignored);
                return false;
            }
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = "保存配置失败：" + std::string(exception.what());
            return false;
        }
    }
}
