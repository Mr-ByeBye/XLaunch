#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace xlaunch
{
    enum class ItemType
    {
        Executable,
        File,
        Folder,
        Url,
        Shortcut,
        Shell
    };

    struct AppearanceSettings
    {
        bool showNames = true;
        bool showBorders = false;
        int iconSize = 48;
        float horizontalSpacing = 12.0f;
        float verticalSpacing = 12.0f;
        float windowOpacity = 1.0f;
        bool fitWindowToGridAfterResize = true;
    };

    enum class StartupPositionMode
    {
        Center,
        Corner,
        Custom,
        Cursor
    };

    enum class ScreenCorner
    {
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    struct WindowSettings
    {
        std::string title = "XLaunch";
        bool centerTitle = true;
        bool keepVisible = false;
        StartupPositionMode startupPosition = StartupPositionMode::Center;
        ScreenCorner corner = ScreenCorner::TopRight;
        int customX = 100;
        int customY = 100;
        int width = 760;
        int height = 500;
    };

    enum class HotkeyTrigger
    {
        MouseGesture,
        Keyboard
    };

    enum class MouseButton { None, Middle, X1, X2 };

    inline constexpr int HotkeyControl = 1 << 0;
    inline constexpr int HotkeyAlt = 1 << 1;
    inline constexpr int HotkeyShift = 1 << 2;
    inline constexpr int HotkeyWin = 1 << 3;

    struct HotkeySettings
    {
        bool enabled = true;
        HotkeyTrigger trigger = HotkeyTrigger::MouseGesture;
        int modifiers = HotkeyControl | HotkeyAlt;
        int virtualKey = 0x20;
        MouseButton mouseButton = MouseButton::Middle;
        MouseButton heldMouseButton = MouseButton::None;
        bool mouseDoubleClick = false;
    };

    struct BackupSettings
    {
        bool automatic = true;
        int keepCount = 10;
    };

    struct LaunchItem
    {
        struct Shortcut
        {
            bool enabled = false;
            int modifiers = 0;
            int virtualKey = 0;
        };

        std::string id;
        std::string automaticName;
        std::string customName;
        std::string target;
        std::string arguments;
        std::string workingDirectory;
        std::string customIconPath;
        bool runAsAdministrator = false;
        ItemType type = ItemType::File;
        int sortOrder = 0;
        Shortcut globalShortcut;
        Shortcut localShortcut;

        [[nodiscard]] const std::string& DisplayName() const
        {
            return customName.empty() ? automaticName : customName;
        }
    };

    struct Category
    {
        std::string id;
        std::string name;
        std::vector<LaunchItem> items;
    };

    struct AppConfig
    {
        int version = 2;
        AppearanceSettings appearance;
        WindowSettings window;
        HotkeySettings hotkey;
        bool startWithWindows = false;
        BackupSettings backup;
        std::vector<Category> categories;
    };

    inline std::string MakeId(const char* prefix)
    {
        static unsigned long long sequence = 0;
        const auto ticks = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        return std::string(prefix) + "-" + std::to_string(ticks) + "-" + std::to_string(++sequence);
    }

    inline std::string DeriveAutomaticName(const std::string& target)
    {
        if (target.empty())
            return "未命名项目";

        if (target.find("://") != std::string::npos)
            return target;

        const std::u8string utf8Target(target.begin(), target.end());
        const std::filesystem::path path(utf8Target);
        const auto filename = path.filename().u8string();
        std::string name(filename.begin(), filename.end());
        if (name.empty())
        {
            const auto parentName = path.parent_path().filename().u8string();
            name.assign(parentName.begin(), parentName.end());
        }
        return name.empty() ? target : name;
    }

    inline ItemType DetectItemType(const std::string& target)
    {
        if (target.rfind("shell:", 0) == 0 || target.rfind("::{", 0) == 0)
            return ItemType::Shell;
        if (target.find("://") != std::string::npos)
            return ItemType::Url;

        const std::u8string utf8Target(target.begin(), target.end());
        const std::filesystem::path path(utf8Target);
        std::error_code error;
        if (std::filesystem::is_directory(path, error))
            return ItemType::Folder;

        const auto extensionValue = path.extension().u8string();
        std::string extension(extensionValue.begin(), extensionValue.end());
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (extension == ".exe")
            return ItemType::Executable;
        if (extension == ".lnk")
            return ItemType::Shortcut;
        return ItemType::File;
    }

    inline const char* ItemTypeName(ItemType type)
    {
        switch (type)
        {
        case ItemType::Executable: return "exe";
        case ItemType::Folder: return "folder";
        case ItemType::Url: return "url";
        case ItemType::Shortcut: return "lnk";
        case ItemType::Shell: return "shell";
        default: return "file";
        }
    }

    inline AppConfig MakeDefaultConfig()
    {
        AppConfig config;
        config.categories.push_back(Category{ MakeId("category"), "常用", {} });
        return config;
    }

    inline void NormalizeConfig(AppConfig& config)
    {
        config.version = 2;
        if (config.categories.empty())
            config.categories.push_back(Category{ MakeId("category"), "常用", {} });

        config.appearance.iconSize = std::clamp(config.appearance.iconSize, 32, 64);
        config.appearance.horizontalSpacing = std::clamp(config.appearance.horizontalSpacing, 4.0f, 40.0f);
        config.appearance.verticalSpacing = std::clamp(config.appearance.verticalSpacing, 4.0f, 40.0f);
        config.appearance.windowOpacity = std::clamp(config.appearance.windowOpacity, 0.35f, 1.0f);
        config.backup.keepCount = std::clamp(config.backup.keepCount, 1, 50);
        config.hotkey.modifiers &= HotkeyControl | HotkeyAlt | HotkeyShift | HotkeyWin;
        if (config.hotkey.virtualKey < 0x08 || config.hotkey.virtualKey > 0xFE)
            config.hotkey.virtualKey = 0x20;
        if (config.window.title.empty()) config.window.title = "XLaunch";

        for (Category& category : config.categories)
        {
            if (category.id.empty())
                category.id = MakeId("category");
            if (category.name.empty())
                category.name = "未命名分类";
            for (LaunchItem& item : category.items)
            {
                if (item.id.empty())
                    item.id = MakeId("item");
                if (item.automaticName.empty())
                    item.automaticName = DeriveAutomaticName(item.target);
                item.globalShortcut.modifiers &= HotkeyControl | HotkeyAlt | HotkeyShift | HotkeyWin;
                item.localShortcut.modifiers &= HotkeyControl | HotkeyAlt | HotkeyShift | HotkeyWin;
                if (item.globalShortcut.virtualKey < 0x08 || item.globalShortcut.virtualKey > 0xFE)
                    item.globalShortcut.enabled = false;
                if (item.localShortcut.virtualKey < 0x08 || item.localShortcut.virtualKey > 0xFE)
                    item.localShortcut.enabled = false;
            }
            std::stable_sort(category.items.begin(), category.items.end(),
                [](const LaunchItem& left, const LaunchItem& right) { return left.sortOrder < right.sortOrder; });
            for (std::size_t index = 0; index < category.items.size(); ++index)
                category.items[index].sortOrder = static_cast<int>(index);
        }
    }
}
