#pragma once

#include "core/LauncherData.h"

#include <array>
#include <cstddef>
#include <string>

#include <windows.h>

namespace xlaunch
{
    class IconCache;

    class ItemEditor
    {
    public:
        void OpenNew(std::size_t categoryIndex);
        void OpenEdit(const AppConfig& config, std::size_t categoryIndex, std::size_t itemIndex);
        void Close() { open_ = false; capturingShortcut_ = 0; }
        void Draw(HWND owner, AppConfig& config, IconCache& iconCache, bool& changed, bool& saveImmediately);
        [[nodiscard]] bool IsOpen() const { return open_; }
        [[nodiscard]] bool IsCapturingShortcut() const { return capturingShortcut_ != 0; }

    private:
        void FillFrom(const LaunchItem& item);
        void ApplySelectedPath(const std::string& path);
        [[nodiscard]] LaunchItem BuildItem() const;

        bool open_ = false;
        bool openRequested_ = false;
        bool editing_ = false;
        std::size_t categoryIndex_ = 0;
        std::size_t itemIndex_ = 0;
        std::string existingId_;
        std::string automaticName_;
        ItemType type_ = ItemType::File;
        std::string validationError_;
        std::array<char, 256> customName_{};
        std::array<char, 2048> target_{};
        std::array<char, 1024> arguments_{};
        std::array<char, 2048> workingDirectory_{};
        std::array<char, 2048> customIconPath_{};
        bool runAsAdministrator_ = false;
        LaunchItem::Shortcut globalShortcut_;
        LaunchItem::Shortcut localShortcut_;
        int capturingShortcut_ = 0;
        int captureStartFrame_ = 0;
    };
}
