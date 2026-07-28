#include "platform/LaunchOperations.h"
#include "platform/PortablePath.h"
#include "platform/FileDropTarget.h"
#include "platform/HotkeyManager.h"
#include "platform/LaunchItemFactory.h"
#include "platform/ShellIcon.h"
#include "renderer/IconCache.h"

#include <chrono>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <d3d11.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

namespace
{
    using Microsoft::WRL::ComPtr;

    bool Check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }

    std::string WideToUtf8(const std::wstring& value)
    {
        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    bool CreateShortcut(const std::filesystem::path& shortcut, const std::filesystem::path& target)
    {
        ComPtr<IShellLinkW> link;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
            return false;
        link->SetPath(target.c_str());
        link->SetArguments(L"/c exit 0");
        link->SetWorkingDirectory(target.parent_path().c_str());
        ComPtr<IPersistFile> persist;
        return SUCCEEDED(link.As(&persist)) && SUCCEEDED(persist->Save(shortcut.c_str(), TRUE));
    }

    class HDropDataObject final : public IDataObject
    {
    public:
        explicit HDropDataObject(std::vector<std::wstring> paths) : paths_(std::move(paths)) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
        {
            if (object == nullptr) return E_POINTER;
            if (id == IID_IUnknown || id == IID_IDataObject) { *object = static_cast<IDataObject*>(this); AddRef(); return S_OK; }
            *object = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
        ULONG STDMETHODCALLTYPE Release() override { const ULONG value = --references_; if (value == 0) delete this; return value; }
        HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override
        {
            if (format == nullptr || medium == nullptr || format->cfFormat != CF_HDROP || !(format->tymed & TYMED_HGLOBAL))
                return DV_E_FORMATETC;
            std::size_t characters = 1;
            for (const std::wstring& path : paths_) characters += path.size() + 1;
            const SIZE_T bytes = sizeof(DROPFILES) + characters * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GHND | GMEM_SHARE, bytes);
            if (memory == nullptr) return E_OUTOFMEMORY;
            auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
            drop->pFiles = sizeof(DROPFILES);
            drop->fWide = TRUE;
            wchar_t* output = reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(drop) + sizeof(DROPFILES));
            for (const std::wstring& path : paths_)
            {
                std::copy(path.begin(), path.end(), output);
                output += path.size();
                *output++ = L'\0';
            }
            *output = L'\0';
            GlobalUnlock(memory);
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = memory;
            medium->pUnkForRelease = nullptr;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
        {
            return format != nullptr && format->cfFormat == CF_HDROP && (format->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_FORMATETC;
        }
        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* output) override { if (output) output->ptd = nullptr; return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

    private:
        std::atomic<ULONG> references_{ 1 };
        std::vector<std::wstring> paths_;
    };
}

int main(int argc, char** argv)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult))
        return 1;

    wchar_t windowsDirectory[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    const std::filesystem::path systemDirectory = std::filesystem::path(windowsDirectory) / L"System32";
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path temporaryDirectory =
        std::filesystem::temp_directory_path() / ("XLaunchPlatformTest-" + std::to_string(suffix));
    std::filesystem::create_directories(temporaryDirectory);
    const std::filesystem::path textFile = temporaryDirectory / L"sample.cmd";
    const std::filesystem::path shortcut = temporaryDirectory / L"command.lnk";
    const std::filesystem::path internetShortcut = temporaryDirectory / L"website.url";
    std::ofstream(textFile) << "@exit /b 0\n";
    std::ofstream(internetShortcut) << "[InternetShortcut]\nURL=file:///C:/Windows/win.ini\n";
    bool success = Check(CreateShortcut(shortcut, systemDirectory / L"cmd.exe"), "无法创建测试快捷方式");

    const std::filesystem::path portableDirectory = xlaunch::ExecutableDirectory() /
        (L"portable-test-" + std::to_wstring(suffix));
    const std::filesystem::path portableFile = portableDirectory / L"tool.exe";
    std::filesystem::create_directories(portableDirectory);
    std::ofstream(portableFile).put('\0');
    const std::string portableStoredPath = xlaunch::MakePortablePath(WideToUtf8(portableFile.wstring()));
    success &= Check(!std::filesystem::path(portableStoredPath).is_absolute(), "EXE 同目录目标未转换为相对路径");
    success &= Check(xlaunch::ResolvePortablePath(portableStoredPath) == WideToUtf8(portableFile.lexically_normal().wstring()),
        "相对路径未正确解析到 EXE 目录");

    std::vector<xlaunch::LaunchItem> items;
    auto add = [&](const char* id, const std::filesystem::path& target)
    {
        xlaunch::LaunchItem item;
        item.id = id;
        item.target = WideToUtf8(target.wstring());
        item.automaticName = xlaunch::DeriveAutomaticName(item.target);
        item.type = xlaunch::DetectItemType(item.target);
        items.push_back(std::move(item));
    };
    add("exe", systemDirectory / L"cmd.exe");
    items.back().arguments = "/c exit 0";
    add("folder", temporaryDirectory);
    add("file", textFile);
    xlaunch::LaunchItem url;
    url.id = "url";
    url.target = "file:///C:/Windows/win.ini";
    url.automaticName = "本地 URL 测试";
    url.type = xlaunch::ItemType::Url;
    items.push_back(std::move(url));
    add("lnk", shortcut);
    xlaunch::LaunchItem custom = items.front();
    custom.id = "custom";
    custom.customIconPath = WideToUtf8((std::filesystem::current_path() / L"Xlaunch_ico.ico").wstring());
    items.push_back(std::move(custom));

    const std::vector<std::pair<std::filesystem::path, xlaunch::ItemType>> factoryCases{
        { systemDirectory / L"cmd.exe", xlaunch::ItemType::Executable },
        { temporaryDirectory, xlaunch::ItemType::Folder },
        { textFile, xlaunch::ItemType::File },
        { shortcut, xlaunch::ItemType::Shortcut },
        { internetShortcut, xlaunch::ItemType::Url }
    };
    std::vector<xlaunch::LaunchItem> parsedItems;
    for (const auto& [path, expectedType] : factoryCases)
    {
        const xlaunch::LaunchItemResult result = xlaunch::CreateLaunchItemFromPath(path.wstring());
        success &= Check(result.success, (std::string("项目解析失败：") + WideToUtf8(path.wstring())).c_str());
        success &= Check(result.item.type == expectedType, "项目类型识别错误");
        success &= Check(!result.item.automaticName.empty(), "自动名称为空");
        if (expectedType == xlaunch::ItemType::Shortcut)
        {
            success &= Check(result.item.target.find("cmd.exe") != std::string::npos, "lnk 未保存真实目标");
            success &= Check(result.item.arguments == "/c exit 0", "lnk 参数未解析");
            success &= Check(!result.item.workingDirectory.empty(), "lnk 工作目录未解析");
        }
        if (expectedType == xlaunch::ItemType::Url)
            success &= Check(result.item.target.find("file:///") == 0, ".url 内容未解析");
        parsedItems.push_back(result.item);
    }
    parsedItems.front().arguments = "/c exit 0";

    const auto shellItem = xlaunch::CreateLaunchItemFromShellItem({}, L"shell:AppsFolder", L"应用");
    success &= Check(shellItem.success && shellItem.item.type == xlaunch::ItemType::Shell &&
        shellItem.item.target == "shell:AppsFolder" && shellItem.item.automaticName == "应用",
        "Windows Shell 项目创建失败");
    PIDLIST_ABSOLUTE appsFolderIdList = nullptr;
    success &= Check(SUCCEEDED(SHParseDisplayName(L"shell:AppsFolder", nullptr, &appsFolderIdList, 0, nullptr)) &&
        appsFolderIdList != nullptr, "AppsFolder Shell 解析失败");
    if (appsFolderIdList != nullptr)
        CoTaskMemFree(appsFolderIdList);

    bool dragActive = false;
    std::vector<xlaunch::DroppedShellItem> droppedItems;
    auto* dropTarget = new xlaunch::FileDropTarget(
        [&](bool active, POINTL) { dragActive = active; },
        [&](std::vector<xlaunch::DroppedShellItem> items, POINTL) { droppedItems = std::move(items); });
    auto* dataObject = new HDropDataObject({
        (systemDirectory / L"cmd.exe").wstring(), temporaryDirectory.wstring(), textFile.wstring(), shortcut.wstring(), internetShortcut.wstring()
    });
    DWORD dropEffect = DROPEFFECT_COPY;
    dropTarget->DragEnter(dataObject, 0, POINTL{}, &dropEffect);
    success &= Check(dragActive && dropEffect == DROPEFFECT_COPY, "OLE DragEnter 未接受 CF_HDROP");
    dropTarget->Drop(dataObject, 0, POINTL{}, &dropEffect);
    success &= Check(!dragActive && droppedItems.size() == 5, "OLE 多项目 Drop 数据不完整");
    dataObject->Release();
    dropTarget->Release();

    xlaunch::HotkeyManager hotkeyManager;
    xlaunch::HotkeySettings hotkeySettings;
    hotkeySettings.enabled = true;
    hotkeySettings.trigger = xlaunch::HotkeyTrigger::Keyboard;
    hotkeySettings.modifiers = xlaunch::HotkeyControl | xlaunch::HotkeyShift;
    hotkeySettings.virtualKey = VK_F12;
    std::string hotkeyError;
    success &= Check(hotkeyManager.Apply(GetConsoleWindow(), hotkeySettings, hotkeyError), hotkeyError.c_str());
    xlaunch::AppConfig itemHotkeyConfig;
    itemHotkeyConfig.categories.push_back({ "hotkey-category", "Hotkeys", {} });
    xlaunch::LaunchItem hotkeyItem;
    hotkeyItem.id = "hotkey-item";
    hotkeyItem.automaticName = "Hotkey item";
    hotkeyItem.globalShortcut = { true, xlaunch::HotkeyControl | xlaunch::HotkeyShift, VK_F11 };
    itemHotkeyConfig.categories.front().items.push_back(hotkeyItem);
    success &= Check(hotkeyManager.ApplyItemHotkeys(GetConsoleWindow(), itemHotkeyConfig, hotkeyError), hotkeyError.c_str());
    const std::string* registeredItemId = hotkeyManager.ItemIdForHotkey(1000);
    success &= Check(registeredItemId != nullptr && *registeredItemId == "hotkey-item", "项目全局快捷键映射错误");
    hotkeyManager.Stop();
    hotkeySettings.trigger = xlaunch::HotkeyTrigger::MouseGesture;
    hotkeySettings.mouseButton = xlaunch::MouseButton::Middle;
    success &= Check(hotkeyManager.Apply(GetConsoleWindow(), hotkeySettings, hotkeyError), hotkeyError.c_str());
    hotkeyManager.Stop();

    xlaunch::ShortcutInfo shortcutInfo;
    success &= Check(xlaunch::ResolveShortcut(shortcut.wstring(), shortcutInfo), "lnk 解析失败");
    success &= Check(!shortcutInfo.targetPath.empty(), "lnk 目标为空");

    for (const xlaunch::LaunchItem& item : items)
    {
        xlaunch::ShellIconResult icon = xlaunch::LoadShellIcon(item);
        success &= Check(icon.icon != nullptr, (std::string("图标读取失败：") + item.id).c_str());
        if (icon.icon != nullptr)
            DestroyIcon(icon.icon);
    }
    xlaunch::ShellIconResult customIcon = xlaunch::LoadShellIcon(items.back());
    success &= Check(customIcon.icon != nullptr && !customIcon.usedFallback, "自定义图标优先级验证失败");
    success &= Check(customIcon.sourcePath == (std::filesystem::current_path() / L"Xlaunch_ico.ico").wstring(), "自定义图标来源不正确");
    if (customIcon.icon != nullptr)
        DestroyIcon(customIcon.icon);

    xlaunch::ShellIconResult highResolutionIcon = xlaunch::LoadShellIcon(items.front(), 64);
    ICONINFO highResolutionInfo{};
    BITMAP highResolutionBitmap{};
    const bool hasHighResolutionBitmap = highResolutionIcon.icon != nullptr &&
        GetIconInfo(highResolutionIcon.icon, &highResolutionInfo) && highResolutionInfo.hbmColor != nullptr &&
        GetObjectW(highResolutionInfo.hbmColor, sizeof(highResolutionBitmap), &highResolutionBitmap) != 0;
    success &= Check(hasHighResolutionBitmap && highResolutionBitmap.bmWidth >= 48,
        "64px 图标仍然来自低分辨率资源");
    if (highResolutionInfo.hbmColor != nullptr) DeleteObject(highResolutionInfo.hbmColor);
    if (highResolutionInfo.hbmMask != nullptr) DeleteObject(highResolutionInfo.hbmMask);
    if (highResolutionIcon.icon != nullptr) DestroyIcon(highResolutionIcon.icon);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel{};
    success &= Check(SUCCEEDED(D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, &featureLevel, &context)), "无法创建 WARP D3D11 设备");
    if (device != nullptr)
    {
        xlaunch::IconCache cache(device.Get());
        for (const xlaunch::LaunchItem& item : items)
        {
            const xlaunch::CachedIcon first = cache.Get(item, 48);
            const xlaunch::CachedIcon second = cache.Get(item, 48);
            success &= Check(first.texture != nullptr, (std::string("纹理创建失败：") + item.id).c_str());
            success &= Check(first.texture == second.texture, (std::string("缓存未命中：") + item.id).c_str());
        }
    }

    if (argc > 1 && std::string(argv[1]) == "--launch")
    {
        for (const xlaunch::LaunchItem& item : parsedItems)
        {
            const xlaunch::OperationResult result = xlaunch::Launch(item);
            success &= Check(result.success, (std::string("启动失败：") + item.id + " " + result.error).c_str());
        }
    }

    std::error_code ignored;
    std::filesystem::remove(shortcut, ignored);
    std::filesystem::remove(textFile, ignored);
    std::filesystem::remove(internetShortcut, ignored);
    std::filesystem::remove(temporaryDirectory, ignored);
    std::filesystem::remove_all(portableDirectory, ignored);
    CoUninitialize();
    return success ? 0 : 1;
}
