#include "platform/TrayIconManager.h"
#include "localization/LanguageManager.h"

#include <shellapi.h>

namespace xlaunch
{
    TrayIconManager::~TrayIconManager() { Remove(); }

    bool TrayIconManager::Add(HWND window, HICON icon, const wchar_t* tooltip)
    {
        data_.hWnd = window;
        data_.uID = 1;
        data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data_.uCallbackMessage = kTrayMessage;
        data_.hIcon = icon;
        wcsncpy_s(data_.szTip, tooltip, _TRUNCATE);
        added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
        if (added_)
        {
            data_.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &data_);
        }
        return added_;
    }

    void TrayIconManager::UpdateTooltip(const wchar_t* tooltip)
    {
        if (!added_) return;
        wcsncpy_s(data_.szTip, tooltip, _TRUNCATE);
        data_.uFlags = NIF_TIP | NIF_SHOWTIP;
        Shell_NotifyIconW(NIM_MODIFY, &data_);
    }

    void TrayIconManager::Remove()
    {
        if (added_) Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }

    void TrayIconManager::ShowMenu()
    {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) return;
        const std::wstring toggleText = LanguageManager::GetWide(IsWindowVisible(data_.hWnd) ? "隐藏 XLaunch" : "显示 XLaunch");
        const std::wstring settingsText = LanguageManager::GetWide("软件设置");
        const std::wstring authorText = LanguageManager::GetWide("作者主页");
        const std::wstring exitText = LanguageManager::GetWide("退出");
        AppendMenuW(menu, MF_STRING, kTrayToggleCommand, toggleText.c_str());
        AppendMenuW(menu, MF_STRING, kTraySettingsCommand, settingsText.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kTrayAuthorCommand, authorText.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kTrayExitCommand, exitText.c_str());
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(data_.hWnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, data_.hWnd, nullptr);
        PostMessageW(data_.hWnd, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }
}
