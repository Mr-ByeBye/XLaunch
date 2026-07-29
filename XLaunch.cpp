#include "framework.h"
#include "Resource.h"

#include "config/BackupManager.h"
#include "config/ConfigManager.h"
#include "platform/FileDropTarget.h"
#include "platform/HotkeyManager.h"
#include "platform/LaunchItemFactory.h"
#include "platform/LaunchOperations.h"
#include "platform/PortablePath.h"
#include "platform/StartupManager.h"
#include "platform/TrayIconManager.h"
#include "renderer/IconCache.h"
#include "ui/CategoryManager.h"
#include "ui/ItemEditor.h"
#include "ui/SettingsPopup.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_set>

#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_deviceContext = nullptr;
    IDXGISwapChain* g_swapChain = nullptr;
    ID3D11RenderTargetView* g_renderTargetView = nullptr;
    bool g_swapChainOccluded = false;
    UINT g_pendingWidth = 0;
    UINT g_pendingHeight = 0;
    float g_dpiScale = 1.0f;
    bool g_exitRequested = false;
    xlaunch::TrayIconManager* g_trayIcon = nullptr;
    xlaunch::HotkeyManager* g_hotkeyManager = nullptr;
    const xlaunch::AppConfig* g_appConfig = nullptr;
    const bool* g_keepVisible = nullptr;
    bool g_allowLocalHotkeys = false;
    int g_fittedWindowWidth = 0;
    int g_fittedWindowHeight = 0;
    bool g_fitWidthAfterNextFrame = false;
    bool g_fitHeightAfterNextFrame = false;
    bool g_persistWindowSizeAfterFrame = false;
    bool g_horizontalResizeOccurred = false;
    bool g_verticalResizeOccurred = false;
    UINT g_lastSizingEdge = 0;
    bool g_trackingClientMouse = false;
    bool g_trackingNonClientMouse = false;
    bool g_autoHideSuppressed = false;
    constexpr UINT_PTR kMouseLeaveHideTimerId = 0x584C;
    constexpr UINT_PTR kDeferredHideTimerId = 0x584D;
    bool g_deferredHideOutsideOnly = false;

    constexpr ImVec4 kClearColor{ 0.055f, 0.063f, 0.078f, 1.0f };
    constexpr const char* kVersion = "v2026072907";
    std::wstring Utf8ToWide(const std::string& value);
    void ApplyDarkTheme(float dpiScale);
    LRESULT WINAPI ToolWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    bool HasCommandLineArgument(const wchar_t* expected)
    {
        int count = 0;
        wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (arguments == nullptr)
            return false;
        bool found = false;
        for (int index = 1; index < count; ++index)
        {
            if (_wcsicmp(arguments[index], expected) == 0)
            {
                found = true;
                break;
            }
        }
        LocalFree(arguments);
        return found;
    }

    std::string DisplayTitle(const xlaunch::AppConfig& config)
    {
        const std::string title = config.window.title.empty() ? "XLaunch" : config.window.title;
        return title == "XLaunch" ? title + " " + kVersion : title;
    }

    int CurrentKeyboardModifiers()
    {
        int modifiers = 0;
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= xlaunch::HotkeyControl;
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= xlaunch::HotkeyAlt;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= xlaunch::HotkeyShift;
        if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) modifiers |= xlaunch::HotkeyWin;
        return modifiers;
    }

    const xlaunch::LaunchItem* FindItemById(const xlaunch::AppConfig& config, const std::string& id)
    {
        for (const xlaunch::Category& category : config.categories)
            for (const xlaunch::LaunchItem& item : category.items)
                if (item.id == id) return &item;
        return nullptr;
    }

    void LaunchFromShortcut(HWND owner, const xlaunch::LaunchItem& item)
    {
        const xlaunch::OperationResult result = xlaunch::Launch(item);
        if (!result.success)
            MessageBoxW(owner, Utf8ToWide(result.error).c_str(), L"XLaunch 启动失败", MB_OK | MB_ICONERROR);
    }

    void ApplyWindowOpacity(HWND window, float opacity)
    {
        const float normalized = std::clamp(opacity, 0.35f, 1.0f);
        LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (normalized < 0.999f)
        {
            if ((extendedStyle & WS_EX_LAYERED) == 0)
                SetWindowLongPtrW(window, GWL_EXSTYLE, extendedStyle | WS_EX_LAYERED);
            SetLayeredWindowAttributes(window, 0, static_cast<BYTE>(normalized * 255.0f + 0.5f), LWA_ALPHA);
        }
        else if ((extendedStyle & WS_EX_LAYERED) != 0)
        {
            SetWindowLongPtrW(window, GWL_EXSTYLE, extendedStyle & ~WS_EX_LAYERED);
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        }
    }

    void FitWindowToGrid(HWND window, bool fitWidth, bool fitHeight)
    {
        if ((!fitWidth || g_fittedWindowWidth <= 0) && (!fitHeight || g_fittedWindowHeight <= 0))
            return;
        RECT bounds{};
        if (!GetWindowRect(window, &bounds))
            return;
        MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitorInfo))
            return;
        const RECT& work = monitorInfo.rcWork;
        const int minimumWidth = static_cast<int>(300.0f * g_dpiScale);
        const int minimumHeight = static_cast<int>(280.0f * g_dpiScale);
        const int workWidth = static_cast<int>(work.right - work.left);
        const int workHeight = static_cast<int>(work.bottom - work.top);
        const int currentWidth = static_cast<int>(bounds.right - bounds.left);
        const int currentHeight = static_cast<int>(bounds.bottom - bounds.top);
        const int width = fitWidth ? std::clamp(g_fittedWindowWidth, minimumWidth, workWidth) : currentWidth;
        const int height = fitHeight ? std::clamp(g_fittedWindowHeight, minimumHeight, workHeight) : currentHeight;
        const bool anchorRight = g_lastSizingEdge == WMSZ_LEFT || g_lastSizingEdge == WMSZ_TOPLEFT || g_lastSizingEdge == WMSZ_BOTTOMLEFT;
        const bool anchorBottom = g_lastSizingEdge == WMSZ_TOP || g_lastSizingEdge == WMSZ_TOPLEFT || g_lastSizingEdge == WMSZ_TOPRIGHT;
        const int desiredX = fitWidth && anchorRight ? static_cast<int>(bounds.right) - width : static_cast<int>(bounds.left);
        const int desiredY = fitHeight && anchorBottom ? static_cast<int>(bounds.bottom) - height : static_cast<int>(bounds.top);
        const int x = std::clamp(desiredX, static_cast<int>(work.left), static_cast<int>(work.right) - width);
        const int y = std::clamp(desiredY, static_cast<int>(work.top), static_cast<int>(work.bottom) - height);
        SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty())
            return {};
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (size <= 0)
            return {};
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    bool SameTarget(const xlaunch::LaunchItem& left, const xlaunch::LaunchItem& right)
    {
        if (left.type == xlaunch::ItemType::Url || right.type == xlaunch::ItemType::Url)
            return left.target == right.target;
        const std::wstring leftPath = Utf8ToWide(left.target);
        const std::wstring rightPath = Utf8ToWide(right.target);
        return CompareStringOrdinal(leftPath.c_str(), -1, rightPath.c_str(), -1, TRUE) == CSTR_EQUAL;
    }

    void HideMainWindow(HWND window)
    {
        KillTimer(window, kMouseLeaveHideTimerId);
        KillTimer(window, kDeferredHideTimerId);
        g_trackingClientMouse = false;
        g_trackingNonClientMouse = false;
        ShowWindow(window, SW_HIDE);
        SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    }

    void RequestHideMainWindow(HWND window, bool outsideOnly)
    {
        // Never remove the HWND while processing the input event/frame that
        // requested the hide. Finish/cancel any ImGui mouse interaction first,
        // then hide from a later message-loop turn after all buttons are up.
        g_deferredHideOutsideOnly = outsideOnly;
        KillTimer(window, kMouseLeaveHideTimerId);
        PostMessageW(window, WM_CANCELMODE, 0, 0);
        SetTimer(window, kDeferredHideTimerId, 80, nullptr);
    }

    bool IsCursorOutsideWindow(HWND window)
    {
        POINT cursor{};
        RECT bounds{};
        return GetCursorPos(&cursor) && GetWindowRect(window, &bounds) && !PtInRect(&bounds, cursor);
    }

    bool IsAnyMouseButtonDown()
    {
        return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
    }

    void TrackMouseLeave(HWND window, bool nonClient)
    {
        KillTimer(window, kMouseLeaveHideTimerId);
        bool& tracking = nonClient ? g_trackingNonClientMouse : g_trackingClientMouse;
        if (tracking)
            return;
        DWORD flags = TME_LEAVE;
        if (nonClient)
            flags |= TME_NONCLIENT;
        TRACKMOUSEEVENT event{ sizeof(event), flags, window, 0 };
        if (TrackMouseEvent(&event))
            tracking = true;
    }

    struct AppState
    {
        xlaunch::ConfigManager configManager;
        xlaunch::AppConfig config;
        xlaunch::ItemEditor itemEditor;
        xlaunch::CategoryManager categoryManager;
        xlaunch::SettingsPopup settingsPopup;
        xlaunch::IconCache iconCache;
        xlaunch::HotkeyManager hotkeyManager;
        std::size_t selectedCategory = 0;
        bool dirty = false;
        std::chrono::steady_clock::time_point dirtySince{};
        std::string errorMessage;
        bool errorPopupRequested = false;
        bool draggingFiles = false;
        std::size_t externalDropCategory = 0;
        bool itemShortcutCaptureActive = false;

        struct PendingDuplicate
        {
            std::size_t categoryIndex = 0;
            xlaunch::LaunchItem item;
        };
        std::deque<PendingDuplicate> pendingDuplicates;
        bool duplicatePopupRequested = false;

        explicit AppState(ID3D11Device* device)
            : iconCache(device)
        {
            auto result = configManager.Load();
            config = std::move(result.config);
            bool portablePathsChanged = false;
            for (xlaunch::Category& category : config.categories)
                for (xlaunch::LaunchItem& item : category.items)
                    portablePathsChanged |= xlaunch::MakeLaunchItemPortable(item);
            if (portablePathsChanged)
                MarkDirty();
            if (!result.error.empty())
                ShowError(result.error);
        }

        void ShowError(std::string message)
        {
            errorMessage = std::move(message);
            errorPopupRequested = true;
        }

        void MarkDirty()
        {
            xlaunch::NormalizeConfig(config);
            std::unordered_set<std::string> activeIds;
            for (const xlaunch::Category& category : config.categories)
            {
                for (const xlaunch::LaunchItem& item : category.items)
                    activeIds.insert(item.id);
            }
            iconCache.Prune(activeIds);
            dirty = true;
            dirtySince = std::chrono::steady_clock::now();
        }

        void SaveNow()
        {
            if (!dirty)
                return;
            std::string error;
            if (!configManager.Save(config, error))
                ShowError(error);
            else if (config.backup.automatic && !xlaunch::BackupManager::CreateAutomatic(
                configManager.Path(), config.backup.keepCount, false, error))
                ShowError(error);
            dirty = false;
        }

        void ApplyHotkey(HWND window)
        {
            std::string error;
            if (!hotkeyManager.Apply(window, config.hotkey, error))
            {
                config.hotkey.enabled = false;
                MarkDirty();
                ShowError(error);
            }
        }

        void ApplyItemHotkeys(HWND window)
        {
            std::string error;
            if (!hotkeyManager.ApplyItemHotkeys(window, config, error))
                ShowError(error);
        }

        void ApplyStartupSetting()
        {
            std::string error;
            if (!xlaunch::StartupManager::SetEnabled(config.startWithWindows, error))
            {
                config.startWithWindows = xlaunch::StartupManager::IsEnabled();
                MarkDirty();
                ShowError(error);
            }
        }

        void HandleSettingsActions(HWND owner, const xlaunch::SettingsActions& actions)
        {
            if (actions.suspendHotkey) hotkeyManager.Stop();
            if (actions.hotkeyChanged)
            {
                ApplyHotkey(owner);
                ApplyItemHotkeys(owner);
            }
            if (actions.startupChanged) ApplyStartupSetting();
            if (actions.windowTitleChanged)
            {
                const std::wstring title = Utf8ToWide(DisplayTitle(config));
                SetWindowTextW(owner, title.c_str());
                if (g_trayIcon != nullptr) g_trayIcon->UpdateTooltip(title.c_str());
            }
            if (actions.windowOpacityChanged)
                ApplyWindowOpacity(owner, config.appearance.windowOpacity);
            if (actions.openConfigDirectory)
            {
                std::error_code directoryError;
                const std::filesystem::path directory = configManager.Path().parent_path();
                std::filesystem::create_directories(directory, directoryError);
                if (directoryError || reinterpret_cast<INT_PTR>(ShellExecuteW(
                    owner, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                    ShowError("无法打开配置目录。");
            }

            if (actions.backupNow || actions.exportConfig || actions.importConfig)
            {
                MarkDirty();
                SaveNow();
            }

            std::string error;
            if (actions.backupNow && !xlaunch::BackupManager::CreateAutomatic(
                configManager.Path(), config.backup.keepCount, true, error))
                ShowError(error);

            if (actions.exportConfig)
            {
                if (const auto destination = xlaunch::BackupManager::ChooseExportPath(owner);
                    destination && !xlaunch::BackupManager::Export(configManager.Path(), *destination, error))
                    ShowError(error);
            }

            if (actions.importConfig)
            {
                if (const auto source = xlaunch::BackupManager::ChooseImportPath(owner))
                {
                    if (!xlaunch::BackupManager::CreateAutomatic(configManager.Path(), config.backup.keepCount, true, error))
                    {
                        ShowError(error);
                        return;
                    }
                    xlaunch::ConfigManager importManager(*source);
                    auto imported = importManager.Load();
                    if (!imported.error.empty())
                    {
                        ShowError("导入失败，当前配置未被修改：" + imported.error);
                        return;
                    }
                    config = std::move(imported.config);
                    selectedCategory = 0;
                    MarkDirty();
                    SaveNow();
                    ApplyHotkey(owner);
                    ApplyItemHotkeys(owner);
                    ApplyStartupSetting();
                    ApplyWindowOpacity(owner, config.appearance.windowOpacity);
                    const std::wstring title = Utf8ToWide(DisplayTitle(config));
                    SetWindowTextW(owner, title.c_str());
                    if (g_trayIcon != nullptr) g_trayIcon->UpdateTooltip(title.c_str());
                }
            }
        }

        void SaveIfDue()
        {
            if (dirty && std::chrono::steady_clock::now() - dirtySince >= std::chrono::milliseconds(250))
                SaveNow();
        }

        void UpdateExternalDropTarget(bool dragging, POINTL point)
        {
            draggingFiles = dragging;
            if (dragging && !config.categories.empty())
                externalDropCategory = categoryManager.HitTestCategory(point, selectedCategory);
        }

        void HandleDroppedFiles(const std::vector<xlaunch::DroppedShellItem>& droppedItems, std::size_t categoryIndex)
        {
            if (categoryIndex >= config.categories.size())
                return;

            bool added = false;
            std::string errors;
            xlaunch::Category& category = config.categories[categoryIndex];
            for (const xlaunch::DroppedShellItem& droppedItem : droppedItems)
            {
                xlaunch::LaunchItemResult result = xlaunch::CreateLaunchItemFromShellItem(
                    droppedItem.fileSystemPath, droppedItem.parsingName, droppedItem.displayName);
                if (!result.success)
                {
                    if (!errors.empty())
                        errors += "\n";
                    errors += result.error;
                    continue;
                }

                const bool duplicate = std::any_of(
                    category.items.begin(), category.items.end(),
                    [&](const xlaunch::LaunchItem& existing) { return SameTarget(existing, result.item); });
                if (duplicate)
                {
                    pendingDuplicates.push_back(PendingDuplicate{ categoryIndex, std::move(result.item) });
                    duplicatePopupRequested = true;
                }
                else
                {
                    result.item.sortOrder = static_cast<int>(category.items.size());
                    category.items.push_back(std::move(result.item));
                    added = true;
                }
            }

            if (added)
            {
                MarkDirty();
                SaveNow();
            }
            if (!errors.empty())
                ShowError(std::move(errors));
        }
    };

    void CreateRenderTarget()
    {
        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        {
            g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTargetView);
            backBuffer->Release();
        }
    }

    void CleanupRenderTarget()
    {
        if (g_renderTargetView != nullptr)
        {
            g_renderTargetView->Release();
            g_renderTargetView = nullptr;
        }
    }

    bool CreateDeviceD3D(HWND window)
    {
        DXGI_SWAP_CHAIN_DESC description{};
        // Keep the conventional double-buffered DX11 swap chain. A single
        // buffer saves very little memory and is unreliable on some drivers,
        // where the first presented frame can remain transparent.
        description.BufferCount = 2;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.OutputWindow = window;
        description.SampleDesc.Count = 1;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL featureLevels[]{
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0,
        };
        D3D_FEATURE_LEVEL selectedFeatureLevel{};
        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
            &description, &g_swapChain, &g_device, &selectedFeatureLevel, &g_deviceContext);
        if (result == DXGI_ERROR_UNSUPPORTED)
        {
            result = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                &description, &g_swapChain, &g_device, &selectedFeatureLevel, &g_deviceContext);
        }
        if (FAILED(result))
            return false;
        CreateRenderTarget();
        return g_renderTargetView != nullptr;
    }

    class ToolWindow
    {
    public:
        bool Initialize(HINSTANCE instance, HWND owner, const wchar_t* title, int width, int height,
            int minimumWidth, int minimumHeight, xlaunch::ToolWindowPosition* savedPosition)
        {
            owner_ = owner;
            savedPosition_ = savedPosition;
            minimumWidth_ = minimumWidth;
            minimumHeight_ = minimumHeight;
            static bool windowClassRegistered = false;
            if (!windowClassRegistered)
            {
                const WNDCLASSEXW windowClass{
                    sizeof(WNDCLASSEXW), CS_CLASSDC, ToolWndProc, 0, 0, instance,
                    LoadIconW(instance, MAKEINTRESOURCEW(IDI_XLAUNCH)), LoadCursorW(nullptr, IDC_ARROW),
                    nullptr, nullptr, L"XLaunchToolWindowClass", LoadIconW(instance, MAKEINTRESOURCEW(IDI_SMALL))
                };
                if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                    return false;
                windowClassRegistered = true;
            }

            window_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"XLaunchToolWindowClass", title,
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
                CW_USEDEFAULT, CW_USEDEFAULT, width, height, owner, nullptr, instance, this);
            if (window_ == nullptr)
                return false;

            IDXGIDevice* dxgiDevice = nullptr;
            IDXGIAdapter* adapter = nullptr;
            IDXGIFactory* factory = nullptr;
            HRESULT result = g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
            if (SUCCEEDED(result)) result = dxgiDevice->GetAdapter(&adapter);
            if (SUCCEEDED(result)) result = adapter->GetParent(IID_PPV_ARGS(&factory));
            if (SUCCEEDED(result))
            {
                DXGI_SWAP_CHAIN_DESC description{};
                description.BufferCount = 2;
                description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                description.OutputWindow = window_;
                description.SampleDesc.Count = 1;
                description.Windowed = TRUE;
                description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
                result = factory->CreateSwapChain(g_device, &description, &swapChain_);
            }
            if (factory != nullptr) factory->Release();
            if (adapter != nullptr) adapter->Release();
            if (dxgiDevice != nullptr) dxgiDevice->Release();
            if (FAILED(result) || !CreateRenderTarget())
                return false;

            ImGuiContext* previous = ImGui::GetCurrentContext();
            context_ = ImGui::CreateContext();
            ImGui::SetCurrentContext(context_);
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.IniFilename = nullptr;
            const float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(window_);
            ApplyDarkTheme(scale);
            ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 15.0f, nullptr,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            if (font == nullptr) io.Fonts->AddFontDefault();
            ImGui_ImplWin32_Init(window_);
            ImGui_ImplDX11_Init(g_device, g_deviceContext);
            ImGui::SetCurrentContext(previous);
            return true;
        }

        void Shutdown()
        {
            if (context_ != nullptr)
            {
                ImGuiContext* previous = ImGui::GetCurrentContext();
                ImGui::SetCurrentContext(context_);
                ImGui_ImplDX11_Shutdown();
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext(context_);
                context_ = nullptr;
                ImGui::SetCurrentContext(previous);
            }
            CleanupRenderTarget();
            if (swapChain_ != nullptr) { swapChain_->Release(); swapChain_ = nullptr; }
            if (window_ != nullptr) { DestroyWindow(window_); window_ = nullptr; }
        }

        void Show()
        {
            if (visible_ || window_ == nullptr) return;
            visible_ = true;
            RECT bounds{};
            GetWindowRect(window_, &bounds);
            RECT ownerBounds{};
            GetWindowRect(owner_, &ownerBounds);
            HMONITOR monitor = savedPosition_ != nullptr && savedPosition_->saved
                ? MonitorFromPoint(POINT{ savedPosition_->x, savedPosition_->y }, MONITOR_DEFAULTTONEAREST)
                : MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{ sizeof(info) };
            GetMonitorInfoW(monitor, &info);
            const int width = bounds.right - bounds.left;
            const int height = bounds.bottom - bounds.top;
            int x = 0;
            int y = 0;
            if (savedPosition_ != nullptr && savedPosition_->saved)
            {
                x = savedPosition_->x;
                y = savedPosition_->y;
            }
            else
            {
                constexpr int gap = 12;
                const int leftSpace = ownerBounds.left - info.rcWork.left;
                const int rightSpace = info.rcWork.right - ownerBounds.right;
                x = rightSpace >= leftSpace ? ownerBounds.right + gap : ownerBounds.left - width - gap;
                y = ownerBounds.top;
            }
            x = std::clamp(x, static_cast<int>(info.rcWork.left), static_cast<int>(info.rcWork.right) - width);
            y = std::clamp(y, static_cast<int>(info.rcWork.top), static_cast<int>(info.rcWork.bottom) - height);
            SetWindowPos(window_, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(window_);
        }

        void Hide() { visible_ = false; if (window_ != nullptr) ShowWindow(window_, SW_HIDE); }
        [[nodiscard]] bool IsVisible() const { return visible_; }
        [[nodiscard]] HWND Handle() const { return window_; }
        [[nodiscard]] bool ConsumePositionChanged()
        {
            const bool changed = positionChanged_;
            positionChanged_ = false;
            return changed;
        }

        template <typename DrawFunction>
        void Render(DrawFunction&& draw)
        {
            if (!visible_ || IsIconic(window_) || context_ == nullptr) return;
            if (pendingWidth_ != 0 && pendingHeight_ != 0)
            {
                CleanupRenderTarget();
                swapChain_->ResizeBuffers(0, pendingWidth_, pendingHeight_, DXGI_FORMAT_UNKNOWN, 0);
                pendingWidth_ = pendingHeight_ = 0;
                CreateRenderTarget();
            }
            ImGuiContext* previous = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(context_);
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            draw();
            ImGui::Render();
            const float clearColor[4]{ kClearColor.x, kClearColor.y, kClearColor.z, kClearColor.w };
            g_deviceContext->OMSetRenderTargets(1, &renderTarget_, nullptr);
            g_deviceContext->ClearRenderTargetView(renderTarget_, clearColor);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            swapChain_->Present(1, 0);
            ImGui::SetCurrentContext(previous);
        }

        [[nodiscard]] bool ConsumeCloseRequested()
        {
            const bool requested = closeRequested_;
            closeRequested_ = false;
            return requested;
        }

        LRESULT HandleMessage(HWND messageWindow, UINT message, WPARAM wParam, LPARAM lParam)
        {
            ImGuiContext* previous = ImGui::GetCurrentContext();
            if (context_ != nullptr) ImGui::SetCurrentContext(context_);
            const bool handled = context_ != nullptr && ImGui_ImplWin32_WndProcHandler(messageWindow, message, wParam, lParam);
            ImGui::SetCurrentContext(previous);
            if (handled) return TRUE;
            if (message == WM_SIZE && wParam != SIZE_MINIMIZED)
            {
                pendingWidth_ = LOWORD(lParam);
                pendingHeight_ = HIWORD(lParam);
                return 0;
            }
            if (message == WM_GETMINMAXINFO)
            {
                auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                info->ptMinTrackSize = POINT{ minimumWidth_, minimumHeight_ };
                return 0;
            }
            if (message == WM_EXITSIZEMOVE && savedPosition_ != nullptr)
            {
                RECT bounds{};
                if (GetWindowRect(messageWindow, &bounds))
                {
                    savedPosition_->saved = true;
                    savedPosition_->x = bounds.left;
                    savedPosition_->y = bounds.top;
                    positionChanged_ = true;
                }
                return 0;
            }
            if (message == WM_CLOSE)
            {
                closeRequested_ = true;
                Hide();
                return 0;
            }
            // During WM_NCCREATE, CreateWindowExW has not returned yet, so the
            // member handle is not assigned. Always use the handle supplied by
            // the window procedure for default processing.
            return DefWindowProcW(messageWindow, message, wParam, lParam);
        }

    private:
        bool CreateRenderTarget()
        {
            ID3D11Texture2D* buffer = nullptr;
            if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return false;
            const HRESULT result = g_device->CreateRenderTargetView(buffer, nullptr, &renderTarget_);
            buffer->Release();
            return SUCCEEDED(result);
        }
        void CleanupRenderTarget()
        {
            if (renderTarget_ != nullptr) { renderTarget_->Release(); renderTarget_ = nullptr; }
        }

        HWND window_ = nullptr;
        HWND owner_ = nullptr;
        xlaunch::ToolWindowPosition* savedPosition_ = nullptr;
        ImGuiContext* context_ = nullptr;
        IDXGISwapChain* swapChain_ = nullptr;
        ID3D11RenderTargetView* renderTarget_ = nullptr;
        UINT pendingWidth_ = 0;
        UINT pendingHeight_ = 0;
        bool visible_ = false;
        bool closeRequested_ = false;
        bool positionChanged_ = false;
        int minimumWidth_ = 620;
        int minimumHeight_ = 420;
    };

    LRESULT WINAPI ToolWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        ToolWindow* tool = reinterpret_cast<ToolWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            tool = static_cast<ToolWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tool));
        }
        return tool != nullptr ? tool->HandleMessage(window, message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
    }

    void CleanupDeviceD3D()
    {
        CleanupRenderTarget();
        if (g_swapChain != nullptr)
        {
            g_swapChain->Release();
            g_swapChain = nullptr;
        }
        if (g_deviceContext != nullptr)
        {
            g_deviceContext->Release();
            g_deviceContext = nullptr;
        }
        if (g_device != nullptr)
        {
            g_device->Release();
            g_device = nullptr;
        }
    }

    void ApplyStartupPosition(HWND window, const xlaunch::AppConfig& config)
    {
        const xlaunch::WindowSettings& settings = config.window;
        RECT bounds{};
        GetWindowRect(window, &bounds);
        const LONG width = bounds.right - bounds.left;
        const LONG height = bounds.bottom - bounds.top;
        constexpr LONG margin = 12;

        POINT cursor{};
        GetCursorPos(&cursor);
        HMONITOR monitor = settings.startupPosition == xlaunch::StartupPositionMode::Cursor
            ? MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST)
            : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
        GetMonitorInfoW(monitor, &monitorInfo);
        const RECT& work = monitorInfo.rcWork;

        LONG x = settings.customX;
        LONG y = settings.customY;
        if (settings.startupPosition == xlaunch::StartupPositionMode::Corner)
        {
            const bool right = settings.corner == xlaunch::ScreenCorner::TopRight ||
                settings.corner == xlaunch::ScreenCorner::BottomRight;
            const bool bottom = settings.corner == xlaunch::ScreenCorner::BottomLeft ||
                settings.corner == xlaunch::ScreenCorner::BottomRight;
            x = right ? work.right - width - margin : work.left + margin;
            y = bottom ? work.bottom - height - margin : work.top + margin;
        }
        else if (settings.startupPosition == xlaunch::StartupPositionMode::Center)
        {
            x = work.left + (work.right - work.left - width) / 2;
            y = work.top + (work.bottom - work.top - height) / 2;
        }
        else if (settings.startupPosition == xlaunch::StartupPositionMode::Cursor)
        {
            x = cursor.x - width / 2;
            y = cursor.y - height / 2;
        }

        x = (std::max)(work.left, (std::min)(x, work.right - width));
        y = (std::max)(work.top, (std::min)(y, work.bottom - height));
        SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void ApplyDarkTheme(float dpiScale)
    {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(10.0f, 8.0f);
        style.FramePadding = ImVec2(10.0f, 6.0f);
        style.ItemSpacing = ImVec2(7.0f, 7.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.WindowRounding = 0.0f;
        style.ChildRounding = 3.0f;
        style.PopupRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 0.0f;
        style.FrameBorderSize = 1.0f;
        style.ScrollbarSize = 7.0f;
        style.ScrollbarRounding = 3.0f;

        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.063f, 0.078f, 1.0f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.063f, 0.071f, 0.087f, 1.0f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.22f, 0.25f, 0.31f, 1.0f);
        style.Colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.95f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.55f, 0.61f, 1.0f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.085f, 0.095f, 0.118f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.14f, 0.16f, 0.20f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.36f, 0.68f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.36f, 0.68f, 1.0f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.42f, 0.76f, 1.0f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.36f, 0.68f, 1.0f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.085f, 0.12f, 0.19f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.27f, 0.49f, 1.0f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.085f, 0.12f, 0.19f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.055f, 0.063f, 0.078f, 0.0f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.27f, 0.33f, 0.65f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.36f, 0.44f, 0.80f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.38f, 0.43f, 0.52f, 0.90f);
        style.ScaleAllSizes(dpiScale);
        style.FontScaleDpi = dpiScale;
    }

    std::string Ellipsize(const std::string& text, float maximumWidth)
    {
        if (ImGui::CalcTextSize(text.c_str()).x <= maximumWidth)
            return text;

        constexpr const char* ellipsis = "...";
        std::size_t end = text.size();
        while (end > 0)
        {
            --end;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
                --end;
            std::string candidate = text.substr(0, end) + ellipsis;
            if (ImGui::CalcTextSize(candidate.c_str()).x <= maximumWidth)
                return candidate;
        }
        return ellipsis;
    }

    bool ShowOperationResult(AppState& state, const xlaunch::OperationResult& result, const char* action)
    {
        if (!result.success)
            state.ShowError(std::string(action) + "失败：" + result.error);
        return result.success;
    }

    void DrawErrorPopup(AppState& state)
    {
        if (state.errorPopupRequested)
        {
            ImGui::OpenPopup("操作失败");
            state.errorPopupRequested = false;
        }
        if (ImGui::BeginPopupModal("操作失败", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushTextWrapPos(480.0f);
            ImGui::TextUnformatted(state.errorMessage.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("确定", ImVec2(90.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void DrawDuplicatePopup(AppState& state)
    {
        if (state.duplicatePopupRequested && !state.pendingDuplicates.empty())
        {
            ImGui::OpenPopup("重复启动项目");
            state.duplicatePopupRequested = false;
        }
        if (!ImGui::BeginPopupModal("重复启动项目", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        AppState::PendingDuplicate& pending = state.pendingDuplicates.front();
        ImGui::Text("“%s”已存在于当前分类，是否仍然添加？", pending.item.DisplayName().c_str());
        if (ImGui::Button("仍然添加", ImVec2(100.0f, 0.0f)))
        {
            if (pending.categoryIndex < state.config.categories.size())
            {
                xlaunch::Category& category = state.config.categories[pending.categoryIndex];
                pending.item.sortOrder = static_cast<int>(category.items.size());
                category.items.push_back(std::move(pending.item));
                state.MarkDirty();
                state.SaveNow();
            }
            state.pendingDuplicates.pop_front();
            state.duplicatePopupRequested = !state.pendingDuplicates.empty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(90.0f, 0.0f)))
        {
            state.pendingDuplicates.pop_front();
            state.duplicatePopupRequested = !state.pendingDuplicates.empty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawIconGrid(HWND owner, AppState& state, bool& changed, bool& saveImmediately)
    {
        enum class PendingAction { None, Duplicate, Move };
        PendingAction pendingAction = PendingAction::None;
        std::size_t pendingItem = 0;
        std::size_t moveDestination = 0;
        int reorderSource = -1;
        int reorderTarget = -1;
        static int deleteItem = -1;

        xlaunch::Category& category = state.config.categories[state.selectedCategory];
        const xlaunch::AppearanceSettings& appearance = state.config.appearance;
        const float iconSize = static_cast<float>(appearance.iconSize) * g_dpiScale;
        const float cellWidth = (std::max)(iconSize + 24.0f * g_dpiScale, 82.0f * g_dpiScale);
        const float cellHeight = iconSize + (appearance.showNames ? 28.0f : 14.0f) * g_dpiScale;
        const float horizontalGap = appearance.horizontalSpacing * g_dpiScale;
        const float verticalGap = appearance.verticalSpacing * g_dpiScale;

        const ImVec2 gridMin = ImGui::GetCursorScreenPos();
        const ImVec2 gridSize = ImGui::GetContentRegionAvail();
        const ImVec2 hostWindowSize = ImGui::GetWindowSize();
        const ImVec2 gridMax{ gridMin.x + gridSize.x, gridMin.y + gridSize.y };
        ImGui::BeginChild(
            "IconGrid",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_None);
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float availableHeight = ImGui::GetContentRegionAvail().y;
        const int columns = (std::max)(1, static_cast<int>((availableWidth + horizontalGap) / (cellWidth + horizontalGap)));
        if (!category.items.empty())
        {
            const int totalRows = static_cast<int>((category.items.size() + static_cast<std::size_t>(columns) - 1) /
                static_cast<std::size_t>(columns));
            const int visibleRows = std::clamp(static_cast<int>(std::lround(
                (availableHeight + verticalGap) / (cellHeight + verticalGap))), 1, totalRows);
            const float fittedGridWidth = columns * cellWidth + (columns - 1) * horizontalGap;
            const float fittedGridHeight = visibleRows * cellHeight + (visibleRows - 1) * verticalGap;
            g_fittedWindowWidth = static_cast<int>(std::ceil(hostWindowSize.x - availableWidth + fittedGridWidth));
            g_fittedWindowHeight = static_cast<int>(std::ceil(hostWindowSize.y - availableHeight + fittedGridHeight));
        }
        else
        {
            g_fittedWindowWidth = 0;
            g_fittedWindowHeight = 0;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(horizontalGap, verticalGap));
        if (category.items.empty())
        {
            const ImVec2 region = ImGui::GetContentRegionAvail();
            const char* message = "将程序、文件或文件夹拖到这里";
            const ImVec2 messageSize = ImGui::CalcTextSize(message);
            ImGui::SetCursorPos(ImVec2(
                (std::max)(0.0f, (region.x - messageSize.x) * 0.5f),
                (std::max)(20.0f, region.y * 0.5f - 12.0f)));
            ImGui::TextDisabled("%s", message);
            const char* hint = "右键添加项目，或直接拖放到这里";
            const ImVec2 hintSize = ImGui::CalcTextSize(hint);
            ImGui::SetCursorPosX((std::max)(0.0f, (region.x - hintSize.x) * 0.5f));
            ImGui::TextDisabled("%s", hint);
        }
        for (std::size_t index = 0; index < category.items.size(); ++index)
        {
            xlaunch::LaunchItem& item = category.items[index];
            ImGui::PushID(static_cast<int>(index));
            if (index % columns != 0)
                ImGui::SameLine();

            const ImVec2 cellMin = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("Item", ImVec2(cellWidth, cellHeight));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                const xlaunch::ItemDragPayload source{ state.selectedCategory, index };
                ImGui::SetDragDropPayload("XLAUNCH_ITEM", &source, sizeof(source));
                ImGui::Text("移动项目：%s", item.DisplayName().c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("XLAUNCH_ITEM"))
                {
                    const auto source = *static_cast<const xlaunch::ItemDragPayload*>(payload->Data);
                    if (source.sourceCategory == state.selectedCategory)
                    {
                        reorderSource = static_cast<int>(source.itemIndex);
                        reorderTarget = static_cast<int>(index);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            const ImVec2 cellMax{ cellMin.x + cellWidth, cellMin.y + cellHeight };
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (hovered)
                drawList->AddRectFilled(cellMin, cellMax, IM_COL32(38, 43, 54, 210), 3.0f);
            if (appearance.showBorders)
                drawList->AddRect(cellMin, cellMax, IM_COL32(65, 72, 88, 255), 3.0f);

            const ImVec2 iconMin{ cellMin.x + (cellWidth - iconSize) * 0.5f, cellMin.y + 4.0f };
            const ImVec2 iconMax{ iconMin.x + iconSize, iconMin.y + iconSize };
            const xlaunch::CachedIcon cachedIcon = state.iconCache.Get(item, static_cast<int>(std::lround(iconSize)));
            if (cachedIcon.texture != nullptr)
            {
                const ImTextureID textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(cachedIcon.texture));
                drawList->AddImage(ImTextureRef(textureId), iconMin, iconMax);
            }
            else
            {
                drawList->AddRectFilled(iconMin, iconMax, IM_COL32(74, 79, 91, 255), 9.0f);
                const ImVec2 glyphSize = ImGui::CalcTextSize("?");
                drawList->AddText(
                    ImVec2(iconMin.x + (iconSize - glyphSize.x) * 0.5f, iconMin.y + (iconSize - glyphSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 245), "?");
            }

            if (appearance.showNames)
            {
                const std::string fullName = item.DisplayName();
                const std::string visibleName = Ellipsize(fullName, cellWidth - 8.0f);
                const float textWidth = ImGui::CalcTextSize(visibleName.c_str()).x;
                drawList->AddText(
                    ImVec2(cellMin.x + (cellWidth - textWidth) * 0.5f, iconMax.y + 4.0f),
                    IM_COL32(230, 233, 239, 255), visibleName.c_str());
            }
            if (hovered)
                ImGui::SetTooltip("%s", item.DisplayName().c_str());

            if (clicked && ImGui::GetDragDropPayload() == nullptr && ShowOperationResult(state, xlaunch::Launch(item), "启动") && !state.config.window.keepVisible)
                RequestHideMainWindow(owner, false);

            if (ImGui::BeginPopupContextItem("ItemMenu"))
            {
                if (ImGui::MenuItem("启动"))
                {
                    if (ShowOperationResult(state, xlaunch::Launch(item), "启动") && !state.config.window.keepVisible)
                        RequestHideMainWindow(owner, false);
                }
                if (ImGui::MenuItem("以管理员身份运行"))
                {
                    if (ShowOperationResult(state, xlaunch::Launch(item, true), "以管理员身份运行") && !state.config.window.keepVisible)
                        RequestHideMainWindow(owner, false);
                }
                if (ImGui::MenuItem("打开所在位置"))
                    ShowOperationResult(state, xlaunch::OpenContainingLocation(item), "打开所在位置");
                ImGui::Separator();
                if (ImGui::MenuItem("编辑"))
                    state.itemEditor.OpenEdit(state.config, state.selectedCategory, index);
                if (ImGui::MenuItem("复制"))
                {
                    pendingAction = PendingAction::Duplicate;
                    pendingItem = index;
                }
                if (ImGui::BeginMenu("移动到分类", state.config.categories.size() > 1))
                {
                    for (std::size_t destination = 0; destination < state.config.categories.size(); ++destination)
                    {
                        if (destination == state.selectedCategory)
                            continue;
                        if (ImGui::MenuItem(state.config.categories[destination].name.c_str()))
                        {
                            pendingAction = PendingAction::Move;
                            pendingItem = index;
                            moveDestination = destination;
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("删除"))
                    deleteItem = static_cast<int>(index);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::PopStyleVar();

        if (ImGui::BeginPopupContextWindow("GridMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("新增启动项目"))
                state.itemEditor.OpenNew(state.selectedCategory);
            if (ImGui::BeginMenu("添加系统图标"))
            {
                struct SystemShellItem
                {
                    const char* name;
                    const char* target;
                };
                constexpr SystemShellItem systemItems[]{
                    { "此电脑", "shell:::{20D04FE0-3AEA-1069-A2D8-08002B30309D}" },
                    { "回收站", "shell:::{645FF040-5081-101B-9F08-00AA002F954E}" },
                    { "控制面板", "shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}" },
                    { "网络", "shell:::{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}" },
                    { "用户文件", "shell:::{59031A47-3F72-44A7-89C5-5595FE6B30EE}" },
                    { "库", "shell:::{031E4825-7B94-4DC3-B131-E946B44C8DD5}" },
                };
                for (const SystemShellItem& systemItem : systemItems)
                {
                    if (!ImGui::MenuItem(systemItem.name))
                        continue;
                    xlaunch::LaunchItem item;
                    item.id = xlaunch::MakeId("item");
                    item.type = xlaunch::ItemType::Shell;
                    item.target = systemItem.target;
                    item.automaticName = systemItem.name;
                    const bool duplicate = std::any_of(category.items.begin(), category.items.end(),
                        [&](const xlaunch::LaunchItem& existing) { return SameTarget(existing, item); });
                    if (duplicate)
                        state.ShowError(std::string("“") + systemItem.name + "”已经在当前分类中。");
                    else
                    {
                        item.sortOrder = static_cast<int>(category.items.size());
                        category.items.push_back(std::move(item));
                        changed = true;
                        saveImmediately = true;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("新增分类"))
                state.categoryManager.OpenAdd();
            if (ImGui::MenuItem("打开设置"))
                state.settingsPopup.Open();
            ImGui::EndPopup();
        }
        ImGui::EndChild();

        if (reorderSource >= 0 && reorderTarget >= 0 && reorderSource != reorderTarget &&
            reorderSource < static_cast<int>(category.items.size()) && reorderTarget < static_cast<int>(category.items.size()))
        {
            xlaunch::LaunchItem moved = std::move(category.items[reorderSource]);
            category.items.erase(category.items.begin() + reorderSource);
            category.items.insert(category.items.begin() + reorderTarget, std::move(moved));
            for (std::size_t index = 0; index < category.items.size(); ++index)
                category.items[index].sortOrder = static_cast<int>(index);
            changed = true;
            saveImmediately = true;
        }

        if (pendingAction == PendingAction::Duplicate && pendingItem < category.items.size())
        {
            xlaunch::LaunchItem copy = category.items[pendingItem];
            copy.id = xlaunch::MakeId("item");
            copy.customName = copy.DisplayName() + " - 副本";
            copy.sortOrder = static_cast<int>(category.items.size());
            category.items.push_back(std::move(copy));
            changed = true;
            saveImmediately = true;
        }
        else if (pendingAction == PendingAction::Move && pendingItem < category.items.size())
        {
            xlaunch::LaunchItem moved = std::move(category.items[pendingItem]);
            category.items.erase(category.items.begin() + static_cast<std::ptrdiff_t>(pendingItem));
            for (std::size_t index = 0; index < category.items.size(); ++index)
                category.items[index].sortOrder = static_cast<int>(index);
            moved.sortOrder = static_cast<int>(state.config.categories[moveDestination].items.size());
            state.config.categories[moveDestination].items.push_back(std::move(moved));
            changed = true;
            saveImmediately = true;
        }

        if (deleteItem >= 0)
            ImGui::OpenPopup("确认删除项目");
        if (ImGui::BeginPopupModal("确认删除项目", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (deleteItem >= 0 && deleteItem < static_cast<int>(category.items.size()))
                ImGui::Text("确认删除“%s”？", category.items[deleteItem].DisplayName().c_str());
            if (ImGui::Button("删除", ImVec2(90.0f, 0.0f)))
            {
                if (deleteItem >= 0 && deleteItem < static_cast<int>(category.items.size()))
                {
                    category.items.erase(category.items.begin() + deleteItem);
                    for (std::size_t index = 0; index < category.items.size(); ++index)
                        category.items[index].sortOrder = static_cast<int>(index);
                    changed = true;
                    saveImmediately = true;
                }
                deleteItem = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(90.0f, 0.0f)))
            {
                deleteItem = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (state.draggingFiles)
        {
            ImDrawList* foreground = ImGui::GetForegroundDrawList();
            foreground->AddRectFilled(gridMin, gridMax, IM_COL32(30, 69, 130, 70), 8.0f);
            foreground->AddRect(gridMin, gridMax, IM_COL32(85, 150, 255, 255), 8.0f, 0, 3.0f);
            const std::size_t targetIndex = (std::min)(state.externalDropCategory, state.config.categories.size() - 1);
            const std::string message = "释放以添加到「" + state.config.categories[targetIndex].name + "」";
            const ImVec2 textSize = ImGui::CalcTextSize(message.c_str());
            const ImVec2 textMin{
                gridMin.x + (gridMax.x - gridMin.x - textSize.x) * 0.5f - 18.0f,
                gridMin.y + (gridMax.y - gridMin.y - textSize.y) * 0.5f - 12.0f
            };
            const ImVec2 textMax{ textMin.x + textSize.x + 36.0f, textMin.y + textSize.y + 24.0f };
            foreground->AddRectFilled(textMin, textMax, IM_COL32(20, 28, 43, 235), 7.0f);
            foreground->AddText(ImVec2(textMin.x + 18.0f, textMin.y + 12.0f), IM_COL32(235, 242, 255, 255), message.c_str());
        }
    }

    void DrawMainWindow(HWND owner, AppState& state)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        static bool mainWindowOpen = true;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, state.config.window.centerTitle ? ImVec2(0.5f, 0.5f) : ImVec2(0.0f, 0.5f));
        const std::string windowLabel = DisplayTitle(state.config) + "###XLaunchMain";
        ImGui::Begin(windowLabel.c_str(), &mainWindowOpen, flags);
        ImGui::PopStyleVar();
        g_allowLocalHotkeys = IsWindowVisible(owner) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!mainWindowOpen)
        {
            mainWindowOpen = true;
            PostMessageW(owner, WM_CLOSE, 0, 0);
        }

        bool changed = false;
        bool saveImmediately = false;
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
        {
            const int direction = ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1;
            const int newSize = std::clamp(state.config.appearance.iconSize + direction * 8, 32, 64);
            if (newSize != state.config.appearance.iconSize)
            {
                state.config.appearance.iconSize = newSize;
                changed = true;
                saveImmediately = true;
            }
        }
        const float categoryWidth = (std::max)(120.0f * g_dpiScale, ImGui::GetContentRegionAvail().x - 62.0f * g_dpiScale);
        xlaunch::ItemMoveRequest itemMove;
        state.categoryManager.Draw(
            owner,
            state.config,
            state.selectedCategory,
            changed,
            itemMove,
            state.draggingFiles,
            state.externalDropCategory,
            categoryWidth,
            g_dpiScale);
        if (itemMove.requested && itemMove.source.sourceCategory < state.config.categories.size() &&
            itemMove.destinationCategory < state.config.categories.size() &&
            itemMove.source.sourceCategory != itemMove.destinationCategory)
        {
            auto& sourceItems = state.config.categories[itemMove.source.sourceCategory].items;
            if (itemMove.source.itemIndex < sourceItems.size())
            {
                xlaunch::LaunchItem moved = std::move(sourceItems[itemMove.source.itemIndex]);
                sourceItems.erase(sourceItems.begin() + static_cast<std::ptrdiff_t>(itemMove.source.itemIndex));
                for (std::size_t index = 0; index < sourceItems.size(); ++index)
                    sourceItems[index].sortOrder = static_cast<int>(index);
                auto& destinationItems = state.config.categories[itemMove.destinationCategory].items;
                moved.sortOrder = static_cast<int>(destinationItems.size());
                destinationItems.push_back(std::move(moved));
                changed = true;
                saveImmediately = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(state.config.window.keepVisible ? "已钉住##Pin" : "钉住##Pin", ImVec2(56.0f * g_dpiScale, 29.0f * g_dpiScale)))
        {
            state.config.window.keepVisible = !state.config.window.keepVisible;
            SetWindowPos(owner, state.config.window.keepVisible ? HWND_TOPMOST : HWND_NOTOPMOST,
                0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            changed = true;
            saveImmediately = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(state.config.window.keepVisible ? "鼠标移出或启动项目后仍保持显示" : "鼠标移出窗口后自动隐藏");
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0f * g_dpiScale);
        ImGui::Separator();
        DrawIconGrid(owner, state, changed, saveImmediately);
        ImGui::End();

        DrawDuplicatePopup(state);
        DrawErrorPopup(state);
        g_autoHideSuppressed = state.itemEditor.IsOpen() || state.settingsPopup.IsOpen() ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (changed)
            state.MarkDirty();
        if (saveImmediately)
        {
            state.SaveNow();
            state.ApplyItemHotkeys(owner);
        }
        else
            state.SaveIfDue();
    }
}

LRESULT WINAPI WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && g_allowLocalHotkeys &&
        (lParam & (1LL << 30)) == 0 && g_appConfig != nullptr)
    {
        const int modifiers = CurrentKeyboardModifiers();
        for (const xlaunch::Category& category : g_appConfig->categories)
        {
            for (const xlaunch::LaunchItem& item : category.items)
            {
                if (item.localShortcut.enabled && item.localShortcut.virtualKey == static_cast<int>(wParam) &&
                    item.localShortcut.modifiers == modifiers)
                {
                    LaunchFromShortcut(window, item);
                    return 0;
                }
            }
        }
    }
    if (message == WM_MOUSEMOVE)
        TrackMouseLeave(window, false);
    else if (message == WM_NCMOUSEMOVE)
        TrackMouseLeave(window, true);
    else if (message == WM_MOUSELEAVE || message == WM_NCMOUSELEAVE)
    {
        if (message == WM_MOUSELEAVE)
            g_trackingClientMouse = false;
        else
            g_trackingNonClientMouse = false;
        ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
        if (g_keepVisible != nullptr && !*g_keepVisible && !g_autoHideSuppressed &&
            IsWindowVisible(window) && IsCursorOutsideWindow(window))
            SetTimer(window, kMouseLeaveHideTimerId, 100, nullptr);
        return 0;
    }
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
        return TRUE;
    switch (message)
    {
    case xlaunch::kTrayMessage:
    {
        const UINT trayEvent = LOWORD(lParam);
        if (trayEvent == WM_CONTEXTMENU && g_trayIcon != nullptr)
            g_trayIcon->ShowMenu();
        else if (trayEvent == WM_LBUTTONDOWN || trayEvent == WM_LBUTTONUP ||
            trayEvent == WM_LBUTTONDBLCLK || trayEvent == NIN_SELECT || trayEvent == NIN_KEYSELECT)
            PostMessageW(window, xlaunch::kShowWindowMessage, 0, 0);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == xlaunch::kTrayToggleCommand) PostMessageW(window, xlaunch::kToggleWindowMessage, 0, 0);
        else if (LOWORD(wParam) == xlaunch::kTrayAuthorCommand)
            ShellExecuteW(window, L"open", L"https://mrx.la", nullptr, nullptr, SW_SHOWNORMAL);
        else if (LOWORD(wParam) == xlaunch::kTrayExitCommand)
        {
            g_exitRequested = true;
            DestroyWindow(window);
        }
        return 0;
    case WM_HOTKEY:
        if (static_cast<int>(wParam) == 1)
            PostMessageW(window, xlaunch::kToggleWindowMessage, 0, 0);
        else if (g_hotkeyManager != nullptr && g_appConfig != nullptr)
        {
            if (const std::string* itemId = g_hotkeyManager->ItemIdForHotkey(static_cast<int>(wParam)))
                if (const xlaunch::LaunchItem* item = FindItemById(*g_appConfig, *itemId))
                    LaunchFromShortcut(window, *item);
        }
        return 0;
    case WM_TIMER:
        if (wParam == kDeferredHideTimerId)
        {
            KillTimer(window, kDeferredHideTimerId);
            if (g_autoHideSuppressed || IsAnyMouseButtonDown())
                SetTimer(window, kDeferredHideTimerId, 80, nullptr);
            else if (GetCapture() == window)
            {
                ReleaseCapture();
                PostMessageW(window, WM_CANCELMODE, 0, 0);
                SetTimer(window, kDeferredHideTimerId, 80, nullptr);
            }
            else if (!g_deferredHideOutsideOnly || IsCursorOutsideWindow(window))
                HideMainWindow(window);
            return 0;
        }
        if (wParam == kMouseLeaveHideTimerId)
        {
            KillTimer(window, kMouseLeaveHideTimerId);
            if (g_autoHideSuppressed || IsAnyMouseButtonDown())
                SetTimer(window, kMouseLeaveHideTimerId, 100, nullptr);
            else if (g_keepVisible != nullptr && !*g_keepVisible && IsWindowVisible(window) && IsCursorOutsideWindow(window))
                RequestHideMainWindow(window, true);
            return 0;
        }
        break;
    case xlaunch::kToggleWindowMessage:
        if (IsWindowVisible(window) && !IsIconic(window) && GetForegroundWindow() == window)
            RequestHideMainWindow(window, false);
        else
        {
            KillTimer(window, kDeferredHideTimerId);
            if (g_appConfig != nullptr && g_appConfig->window.startupPosition == xlaunch::StartupPositionMode::Cursor)
                ApplyStartupPosition(window, *g_appConfig);
            ShowWindow(window, SW_RESTORE);
            SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            if (g_keepVisible == nullptr || !*g_keepVisible)
                SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(window);
        }
        return 0;
    case xlaunch::kShowWindowMessage:
        KillTimer(window, kDeferredHideTimerId);
        if (g_appConfig != nullptr && g_appConfig->window.startupPosition == xlaunch::StartupPositionMode::Cursor)
            ApplyStartupPosition(window, *g_appConfig);
        ShowWindow(window, SW_RESTORE);
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        if (g_keepVisible == nullptr || !*g_keepVisible)
            SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(window);
        return 0;
    case WM_NCHITTEST:
    {
        const LRESULT defaultHit = DefWindowProcW(window, message, wParam, lParam);
        if (defaultHit != HTCLIENT)
            return defaultHit;
        RECT bounds{};
        GetWindowRect(window, &bounds);
        const LONG x = static_cast<LONG>(static_cast<short>(LOWORD(lParam)));
        const LONG y = static_cast<LONG>(static_cast<short>(HIWORD(lParam)));
        const LONG edge = static_cast<LONG>(6.0f * g_dpiScale);
        const bool left = x < bounds.left + edge;
        const bool right = x >= bounds.right - edge;
        const bool top = y < bounds.top + edge;
        const bool bottom = y >= bounds.bottom - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        const LONG titleHeight = static_cast<LONG>(28.0f * g_dpiScale);
        const LONG closeButtonWidth = static_cast<LONG>(30.0f * g_dpiScale);
        if (y < bounds.top + titleHeight && x < bounds.right - closeButtonWidth)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO:
    {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = static_cast<LONG>(300.0f * g_dpiScale);
        limits->ptMinTrackSize.y = static_cast<LONG>(280.0f * g_dpiScale);
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            g_pendingWidth = static_cast<UINT>(LOWORD(lParam));
            g_pendingHeight = static_cast<UINT>(HIWORD(lParam));
        }
        return 0;
    case WM_DPICHANGED:
    {
        const UINT dpi = HIWORD(wParam);
        const float newScale = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
        g_dpiScale = newScale;
        if (ImGui::GetCurrentContext() != nullptr)
            ApplyDarkTheme(newScale);
        if (const auto* suggested = reinterpret_cast<const RECT*>(lParam))
        {
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        g_horizontalResizeOccurred = false;
        g_verticalResizeOccurred = false;
        g_lastSizingEdge = 0;
        return 0;
    case WM_SIZING:
        g_lastSizingEdge = static_cast<UINT>(wParam);
        if (wParam != WMSZ_TOP && wParam != WMSZ_BOTTOM)
            g_horizontalResizeOccurred = true;
        if (wParam != WMSZ_LEFT && wParam != WMSZ_RIGHT)
            g_verticalResizeOccurred = true;
        break;
    case WM_EXITSIZEMOVE:
        if (g_appConfig != nullptr && g_appConfig->appearance.fitWindowToGridAfterResize)
        {
            g_fitWidthAfterNextFrame = g_horizontalResizeOccurred;
            g_fitHeightAfterNextFrame = g_verticalResizeOccurred;
        }
        g_persistWindowSizeAfterFrame = true;
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_CLOSE:
        if (!g_exitRequested)
        {
            HideMainWindow(window);
            return 0;
        }
        break;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wParam)
        {
            g_exitRequested = true;
            PostQuitMessage(0);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int showCommand)
{
    const bool launchedAtStartup = HasCommandLineArgument(L"--startup");
    HANDLE singleInstanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\XLaunch.SingleInstance");
    if (singleInstanceMutex == nullptr)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (HWND existing = FindWindowW(L"XLaunchWindowClass", nullptr))
            {
                if (!launchedAtStartup)
                    PostMessageW(existing, xlaunch::kShowWindowMessage, 0, 0);
                break;
            }
            Sleep(50);
        }
        CloseHandle(singleInstanceMutex);
        return 0;
    }
    ImGui_ImplWin32_EnableDpiAwareness();
    const HMONITOR primaryMonitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    const float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(primaryMonitor);
    g_dpiScale = dpiScale;
    const WNDCLASSEXW windowClass{
        sizeof(WNDCLASSEXW), CS_CLASSDC | CS_DROPSHADOW, WndProc, 0, 0, instance,
        LoadIconW(instance, MAKEINTRESOURCEW(IDI_XLAUNCH)),
        LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"XLaunchWindowClass",
        LoadIconW(instance, MAKEINTRESOURCEW(IDI_SMALL))
    };
    if (RegisterClassExW(&windowClass) == 0)
    {
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    const HWND window = CreateWindowExW(
        WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"XLaunch", WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        static_cast<int>(760.0f * dpiScale), static_cast<int>(500.0f * dpiScale),
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr || !CreateDeviceD3D(window))
    {
        CleanupDeviceD3D();
        if (window != nullptr)
            DestroyWindow(window);
        UnregisterClassW(windowClass.lpszClassName, instance);
        MessageBoxW(nullptr, L"DirectX 11 初始化失败。", L"XLaunch", MB_OK | MB_ICONERROR);
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ApplyDarkTheme(dpiScale);
    ImFont* uiFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc", 15.0f, nullptr,
        io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    if (uiFont == nullptr)
        io.Fonts->AddFontDefault();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(g_device, g_deviceContext);

    const HRESULT oleResult = OleInitialize(nullptr);
    const bool oleInitialized = SUCCEEDED(oleResult);
    AppState state(g_device);
    g_appConfig = &state.config;
    g_keepVisible = &state.config.window.keepVisible;
    g_hotkeyManager = &state.hotkeyManager;
    const std::wstring applicationTitle = Utf8ToWide(DisplayTitle(state.config));
    SetWindowTextW(window, applicationTitle.c_str());
    ApplyWindowOpacity(window, state.config.appearance.windowOpacity);
    state.config.window.width = std::clamp(state.config.window.width, 300, 10000);
    state.config.window.height = std::clamp(state.config.window.height, 280, 10000);
    SetWindowPos(window, nullptr, 0, 0,
        static_cast<int>(state.config.window.width * dpiScale),
        static_cast<int>(state.config.window.height * dpiScale),
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyStartupPosition(window, state.config);
    if (state.config.window.keepVisible)
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    state.ApplyHotkey(window);
    state.ApplyItemHotkeys(window);
    state.ApplyStartupSetting();
    xlaunch::TrayIconManager trayIcon;
    g_trayIcon = &trayIcon;
    if (!trayIcon.Add(window, LoadIconW(instance, MAKEINTRESOURCEW(IDI_XLAUNCH)), applicationTitle.c_str()))
        state.ShowError("创建系统托盘图标失败。");
    ToolWindow settingsWindow;
    ToolWindow itemEditorWindow;
    const bool settingsWindowReady = settingsWindow.Initialize(instance, window, L"XLaunch 设置",
        static_cast<int>(720.0f * dpiScale), static_cast<int>(370.0f * dpiScale),
        static_cast<int>(680.0f * dpiScale), static_cast<int>(350.0f * dpiScale),
        &state.config.window.settingsPosition);
    const bool itemEditorWindowReady = itemEditorWindow.Initialize(instance, window, L"XLaunch 启动项编辑",
        static_cast<int>(760.0f * dpiScale), static_cast<int>(500.0f * dpiScale),
        static_cast<int>(620.0f * dpiScale), static_cast<int>(420.0f * dpiScale),
        &state.config.window.itemEditorPosition);
    if (!settingsWindowReady || !itemEditorWindowReady)
        state.ShowError("创建独立设置或编辑窗口失败。");
    if (!launchedAtStartup)
    {
        ShowWindow(window, showCommand);
        UpdateWindow(window);
    }
    xlaunch::FileDropTarget* dropTarget = nullptr;
    bool dropTargetRegistered = false;
    if (oleInitialized)
    {
        dropTarget = new xlaunch::FileDropTarget(
            [&](bool dragging, POINTL point) { state.UpdateExternalDropTarget(dragging, point); },
            [&](std::vector<xlaunch::DroppedShellItem> items, POINTL point) {
                const std::size_t destination = state.categoryManager.HitTestCategory(point, state.selectedCategory);
                state.HandleDroppedFiles(items, destination);
            });
        const HRESULT registerResult = RegisterDragDrop(window, dropTarget);
        dropTargetRegistered = SUCCEEDED(registerResult);
        if (!dropTargetRegistered)
            state.ShowError("注册 Windows 文件拖放失败，错误代码：" + std::to_string(registerResult));
    }
    else
    {
        state.ShowError("初始化 Windows OLE 拖放失败，错误代码：" + std::to_string(oleResult));
    }
    bool running = true;
    bool hasRenderedFrame = false;
    while (running)
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT)
                running = false;
        }
        if (!running)
            break;
        // Render one frame while a startup-launched window is still hidden. This
        // primes the swap chain so the first hotkey invocation cannot expose an
        // unpainted transparent window while DirectX/ImGui produce their first frame.
        if (settingsWindow.ConsumeCloseRequested())
        {
            state.settingsPopup.Close();
            state.ApplyHotkey(window);
            state.ApplyItemHotkeys(window);
        }
        if (itemEditorWindow.ConsumeCloseRequested()) state.itemEditor.Close();
        if (settingsWindow.ConsumePositionChanged() || itemEditorWindow.ConsumePositionChanged())
        {
            state.MarkDirty();
            state.SaveNow();
        }
        if (state.settingsPopup.IsOpen() && settingsWindowReady) settingsWindow.Show();
        else if (settingsWindow.IsVisible()) settingsWindow.Hide();
        if (state.itemEditor.IsOpen() && itemEditorWindowReady) itemEditorWindow.Show();
        else if (itemEditorWindow.IsVisible()) itemEditorWindow.Hide();

        if (!IsWindowVisible(window) && !settingsWindow.IsVisible() && !itemEditorWindow.IsVisible() && hasRenderedFrame)
        {
            WaitMessage();
            continue;
        }
        if (!settingsWindow.IsVisible() && !itemEditorWindow.IsVisible() && g_swapChainOccluded &&
            g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            Sleep(10);
            continue;
        }
        g_swapChainOccluded = false;
        if (g_pendingWidth != 0 && g_pendingHeight != 0)
        {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_pendingWidth, g_pendingHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_pendingWidth = g_pendingHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawMainWindow(window, state);
        ImGui::Render();
        const float clearColor[4]{ kClearColor.x, kClearColor.y, kClearColor.z, kClearColor.w };
        g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, nullptr);
        g_deviceContext->ClearRenderTargetView(g_renderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT presentResult = g_swapChain->Present(1, 0);
        hasRenderedFrame = true;
        g_swapChainOccluded = presentResult == DXGI_STATUS_OCCLUDED;

        bool toolChanged = false;
        bool toolSaveImmediately = false;
        itemEditorWindow.Render([&]
        {
            state.itemEditor.Draw(itemEditorWindow.Handle(), state.config, state.iconCache, toolChanged, toolSaveImmediately);
        });
        xlaunch::SettingsActions settingsActions;
        settingsWindow.Render([&]
        {
            // Settings such as "use current position" describe the launcher,
            // not the separate settings tool window.
            settingsActions = state.settingsPopup.Draw(window, state.config, toolChanged);
        });
        if (!state.itemEditor.IsOpen()) itemEditorWindow.Hide();
        if (!state.settingsPopup.IsOpen()) settingsWindow.Hide();
        const bool capturingItemShortcut = state.itemEditor.IsCapturingShortcut();
        if (capturingItemShortcut != state.itemShortcutCaptureActive)
        {
            state.itemShortcutCaptureActive = capturingItemShortcut;
            if (capturingItemShortcut)
                state.hotkeyManager.Stop();
            else
            {
                state.ApplyHotkey(window);
                state.ApplyItemHotkeys(window);
            }
        }
        if (toolChanged) state.MarkDirty();
        if (toolSaveImmediately)
        {
            state.SaveNow();
            state.ApplyItemHotkeys(window);
        }
        state.HandleSettingsActions(window, settingsActions);
        if (g_fitWidthAfterNextFrame || g_fitHeightAfterNextFrame)
        {
            const bool fitWidth = g_fitWidthAfterNextFrame;
            const bool fitHeight = g_fitHeightAfterNextFrame;
            g_fitWidthAfterNextFrame = false;
            g_fitHeightAfterNextFrame = false;
            if (state.config.appearance.fitWindowToGridAfterResize)
                FitWindowToGrid(window, fitWidth, fitHeight);
        }
        if (g_persistWindowSizeAfterFrame)
        {
            g_persistWindowSizeAfterFrame = false;
            RECT bounds{};
            if (GetWindowRect(window, &bounds))
            {
                state.config.window.width = (std::max)(300, static_cast<int>(std::lround((bounds.right - bounds.left) / g_dpiScale)));
                state.config.window.height = (std::max)(280, static_cast<int>(std::lround((bounds.bottom - bounds.top) / g_dpiScale)));
                state.MarkDirty();
                state.SaveNow();
            }
        }
    }

    state.SaveNow();
    itemEditorWindow.Shutdown();
    settingsWindow.Shutdown();
    trayIcon.Remove();
    g_trayIcon = nullptr;
    g_appConfig = nullptr;
    g_keepVisible = nullptr;
    g_hotkeyManager = nullptr;
    if (dropTargetRegistered)
        RevokeDragDrop(window);
    if (dropTarget != nullptr)
        dropTarget->Release();
    state.iconCache.Clear();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    if (oleInitialized)
        OleUninitialize();
    CloseHandle(singleInstanceMutex);
    return 0;
}
