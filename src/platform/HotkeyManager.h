#pragma once

#include "core/LauncherData.h"

#include <string>
#include <thread>
#include <unordered_map>
#include <windows.h>

namespace xlaunch
{
    inline constexpr UINT kToggleWindowMessage = WM_APP + 42;
    inline constexpr UINT kShowWindowMessage = WM_APP + 44;

    class HotkeyManager
    {
    public:
        HotkeyManager() = default;
        ~HotkeyManager();

        HotkeyManager(const HotkeyManager&) = delete;
        HotkeyManager& operator=(const HotkeyManager&) = delete;

        [[nodiscard]] bool Apply(HWND window, const HotkeySettings& settings, std::string& error);
        [[nodiscard]] bool ApplyItemHotkeys(HWND window, const AppConfig& config, std::string& error);
        [[nodiscard]] const std::string* ItemIdForHotkey(int hotkeyId) const;
        void Stop();

    private:
        static LRESULT CALLBACK MouseHook(int code, WPARAM wParam, LPARAM lParam);
        void StopToggle();
        void StopItems();

        HWND window_ = nullptr;
        HHOOK mouseHook_ = nullptr;
        bool keyboardRegistered_ = false;
        HotkeySettings settings_;
        bool middleDown_ = false;
        bool x1Down_ = false;
        bool x2Down_ = false;
        bool primaryGesturePending_ = false;
        ULONGLONG lastPrimaryClick_ = 0;
        std::thread mouseThread_;
        DWORD mouseThreadId_ = 0;
        DWORD mouseHookError_ = ERROR_SUCCESS;
        std::unordered_map<int, std::string> itemHotkeys_;
        static HotkeyManager* activeMouseManager_;
    };
}
