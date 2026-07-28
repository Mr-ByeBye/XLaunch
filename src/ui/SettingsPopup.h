#pragma once

#include "core/LauncherData.h"

#include <windows.h>
#include <array>

namespace xlaunch
{
    struct SettingsActions
    {
        bool hotkeyChanged = false;
        bool suspendHotkey = false;
        bool startupChanged = false;
        bool exportConfig = false;
        bool importConfig = false;
        bool backupNow = false;
        bool windowTitleChanged = false;
        bool windowOpacityChanged = false;
        bool openConfigDirectory = false;
    };

    class SettingsPopup
    {
    public:
        void Open() { openRequested_ = true; }
        [[nodiscard]] SettingsActions Draw(HWND owner, AppConfig& config, bool& changed);

    private:
        bool openRequested_ = false;
        bool capturingHotkey_ = false;
        int captureStartFrame_ = 0;
        std::array<char, 128> titleBuffer_{};
    };
}
