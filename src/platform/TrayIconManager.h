#pragma once

#include <windows.h>
#include <shellapi.h>

namespace xlaunch
{
    inline constexpr UINT kTrayMessage = WM_APP + 43;
    inline constexpr UINT kTrayToggleCommand = 41001;
    inline constexpr UINT kTrayAuthorCommand = 41002;
    inline constexpr UINT kTrayExitCommand = 41003;

    class TrayIconManager
    {
    public:
        ~TrayIconManager();
        [[nodiscard]] bool Add(HWND window, HICON icon, const wchar_t* tooltip);
        void UpdateTooltip(const wchar_t* tooltip);
        void Remove();
        void ShowMenu();

    private:
        NOTIFYICONDATAW data_{ sizeof(NOTIFYICONDATAW) };
        bool added_ = false;
    };
}
