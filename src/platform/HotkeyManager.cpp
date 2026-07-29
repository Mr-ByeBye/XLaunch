#include "platform/HotkeyManager.h"

#include <future>

namespace xlaunch
{
    namespace
    {
        constexpr int kHotkeyId = 1;
        constexpr int kFirstItemHotkeyId = 1000;

        UINT WindowsModifiers(int modifiers)
        {
            UINT result = MOD_NOREPEAT;
            if ((modifiers & HotkeyControl) != 0) result |= MOD_CONTROL;
            if ((modifiers & HotkeyAlt) != 0) result |= MOD_ALT;
            if ((modifiers & HotkeyShift) != 0) result |= MOD_SHIFT;
            if ((modifiers & HotkeyWin) != 0) result |= MOD_WIN;
            return result;
        }

        std::string WindowsError(DWORD code)
        {
            return "Windows 错误代码：" + std::to_string(code);
        }
    }

    HotkeyManager* HotkeyManager::activeMouseManager_ = nullptr;

    HotkeyManager::~HotkeyManager()
    {
        Stop();
    }

    bool HotkeyManager::Apply(HWND window, const HotkeySettings& settings, std::string& error)
    {
        StopToggle();
        window_ = window;
        settings_ = settings;
        if (!settings.enabled)
        {
            error.clear();
            return true;
        }

        if (settings.trigger == HotkeyTrigger::MouseGesture)
        {
            std::promise<bool> ready;
            auto result = ready.get_future();
            mouseThread_ = std::thread([this, signal = std::move(ready)]() mutable
            {
                mouseThreadId_ = GetCurrentThreadId();
                MSG message{};
                PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
                activeMouseManager_ = this;
                mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, GetModuleHandleW(nullptr), 0);
                mouseHookError_ = mouseHook_ == nullptr ? GetLastError() : ERROR_SUCCESS;
                signal.set_value(mouseHook_ != nullptr);
                if (mouseHook_ != nullptr)
                {
                    while (GetMessageW(&message, nullptr, 0, 0) > 0)
                    {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                    UnhookWindowsHookEx(mouseHook_);
                    mouseHook_ = nullptr;
                }
                if (activeMouseManager_ == this) activeMouseManager_ = nullptr;
            });
            if (!result.get())
            {
                mouseThread_.join();
                mouseThreadId_ = 0;
                error = "注册全局鼠标手势失败。" + WindowsError(mouseHookError_);
                return false;
            }
            error.clear();
            return true;
        }

        keyboardRegistered_ = RegisterHotKey(window_, kHotkeyId, WindowsModifiers(settings.modifiers), static_cast<UINT>(settings.virtualKey)) != FALSE;
        if (!keyboardRegistered_)
        {
            error = "注册全局键盘快捷键失败，可能已被其他程序占用。" + WindowsError(GetLastError());
            return false;
        }
        error.clear();
        return true;
    }

    bool HotkeyManager::ApplyItemHotkeys(HWND window, const AppConfig& config, std::string& error)
    {
        StopItems();
        window_ = window;
        int hotkeyId = kFirstItemHotkeyId;
        std::string failures;
        for (const Category& category : config.categories)
        {
            for (const LaunchItem& item : category.items)
            {
                if (!item.globalShortcut.enabled)
                    continue;
                if (RegisterHotKey(window_, hotkeyId, WindowsModifiers(item.globalShortcut.modifiers),
                    static_cast<UINT>(item.globalShortcut.virtualKey)) == FALSE)
                {
                    if (!failures.empty()) failures += "\n";
                    failures += "“" + item.DisplayName() + "”的全局快捷键注册失败，可能已被占用。";
                }
                else
                    itemHotkeys_.emplace(hotkeyId, item.id);
                ++hotkeyId;
            }
        }
        error = std::move(failures);
        return error.empty();
    }

    const std::string* HotkeyManager::ItemIdForHotkey(int hotkeyId) const
    {
        const auto found = itemHotkeys_.find(hotkeyId);
        return found == itemHotkeys_.end() ? nullptr : &found->second;
    }

    void HotkeyManager::Stop()
    {
        StopToggle();
        StopItems();
    }

    void HotkeyManager::StopToggle()
    {
        if (keyboardRegistered_ && window_ != nullptr)
            UnregisterHotKey(window_, kHotkeyId);
        keyboardRegistered_ = false;
        if (mouseThread_.joinable())
        {
            PostThreadMessageW(mouseThreadId_, WM_QUIT, 0, 0);
            mouseThread_.join();
        }
        mouseThreadId_ = 0;
        middleDown_ = false;
        x1Down_ = false;
        x2Down_ = false;
        primaryGesturePending_ = false;
        lastPrimaryClick_ = 0;
    }

    void HotkeyManager::StopItems()
    {
        if (window_ != nullptr)
        {
            for (const auto& entry : itemHotkeys_)
                UnregisterHotKey(window_, entry.first);
        }
        itemHotkeys_.clear();
    }

    LRESULT CALLBACK HotkeyManager::MouseHook(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code < 0 || (wParam != WM_MBUTTONDOWN && wParam != WM_MBUTTONUP &&
            wParam != WM_XBUTTONDOWN && wParam != WM_XBUTTONUP))
            return CallNextHookEx(nullptr, code, wParam, lParam);

        if (code >= 0 && activeMouseManager_ != nullptr && activeMouseManager_->window_ != nullptr)
        {
            auto* manager = activeMouseManager_;
            const auto* event = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
            MouseButton button = MouseButton::None;
            bool down = false;
            if (wParam == WM_MBUTTONDOWN || wParam == WM_MBUTTONUP) { button = MouseButton::Middle; down = wParam == WM_MBUTTONDOWN; }
            else if (wParam == WM_XBUTTONDOWN || wParam == WM_XBUTTONUP)
            {
                button = HIWORD(event->mouseData) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
                down = wParam == WM_XBUTTONDOWN;
            }

            auto setDown = [&](MouseButton value, bool state)
            {
                if (value == MouseButton::Middle) manager->middleDown_ = state;
                else if (value == MouseButton::X1) manager->x1Down_ = state;
                else if (value == MouseButton::X2) manager->x2Down_ = state;
            };
            auto isDown = [&](MouseButton value)
            {
                if (value == MouseButton::None) return true;
                if (value == MouseButton::Middle) return manager->middleDown_;
                if (value == MouseButton::X1) return manager->x1Down_;
                return manager->x2Down_;
            };

            if (button != MouseButton::None)
            {
                bool consume = false;
                if (down)
                {
                    const bool heldSatisfied = manager->settings_.heldMouseButton == MouseButton::None ||
                        (manager->settings_.heldMouseButton != button && isDown(manager->settings_.heldMouseButton));
                    if (button == manager->settings_.mouseButton && heldSatisfied)
                    {
                        // A global mouse gesture owns the complete click. Do not
                        // forward only its down half to the window underneath and
                        // let XLaunch consume the corresponding release. Consume
                        // both halves, while still showing immediately on down.
                        manager->primaryGesturePending_ = true;
                        bool trigger = !manager->settings_.mouseDoubleClick;
                        if (manager->settings_.mouseDoubleClick)
                        {
                            const ULONGLONG now = GetTickCount64();
                            trigger = manager->lastPrimaryClick_ != 0 &&
                                now - manager->lastPrimaryClick_ <= GetDoubleClickTime();
                            manager->lastPrimaryClick_ = trigger ? 0 : now;
                        }
                        if (trigger) PostMessageW(manager->window_, kToggleWindowMessage, 0, 0);
                        consume = true;
                    }
                }
                else if (button == manager->settings_.mouseButton && manager->primaryGesturePending_)
                {
                    manager->primaryGesturePending_ = false;
                    consume = true;
                }
                setDown(button, down);
                if (consume)
                    return 1;
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
}
