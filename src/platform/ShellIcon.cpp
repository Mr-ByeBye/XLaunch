#include "platform/ShellIcon.h"
#include "platform/PortablePath.h"

#include <algorithm>
#include <filesystem>
#include <shlobj.h>
#include <shlwapi.h>
#include <commoncontrols.h>
#include <wrl/client.h>

namespace xlaunch
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

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

        HICON ExtractIcon(const std::wstring& path, int iconIndex, int requestedSize)
        {
            if (path.empty())
                return nullptr;
            HICON icon = nullptr;
            UINT identifier = 0;
            return PrivateExtractIconsW(path.c_str(), iconIndex, requestedSize, requestedSize, &icon, &identifier, 1, LR_LOADFROMFILE) > 0
                ? icon : nullptr;
        }

        HICON IconFromShell(const std::wstring& path, DWORD fileAttributes, bool useFileAttributes, int requestedSize)
        {
            SHFILEINFOW info{};
            UINT flags = SHGFI_SYSICONINDEX;
            if (useFileAttributes)
                flags |= SHGFI_USEFILEATTRIBUTES;
            if (SHGetFileInfoW(path.c_str(), fileAttributes, &info, sizeof(info), flags) == 0)
                return nullptr;
            IImageList* imageList = nullptr;
            const int listSize = requestedSize >= 48 ? SHIL_EXTRALARGE : (requestedSize >= 32 ? SHIL_LARGE : SHIL_SMALL);
            if (FAILED(SHGetImageList(listSize, IID_PPV_ARGS(&imageList))) || imageList == nullptr)
                return nullptr;
            HICON icon = nullptr;
            imageList->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon);
            imageList->Release();
            return icon;
        }

        HICON IconFromShellParsingName(const std::wstring& parsingName, int requestedSize)
        {
            PIDLIST_ABSOLUTE itemIdList = nullptr;
            if (FAILED(SHParseDisplayName(parsingName.c_str(), nullptr, &itemIdList, 0, nullptr)) || itemIdList == nullptr)
                return nullptr;
            SHFILEINFOW info{};
            const DWORD_PTR result = SHGetFileInfoW(
                reinterpret_cast<LPCWSTR>(itemIdList), 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_SYSICONINDEX);
            CoTaskMemFree(itemIdList);
            if (result == 0)
                return nullptr;
            IImageList* imageList = nullptr;
            const int listSize = requestedSize >= 48 ? SHIL_EXTRALARGE : (requestedSize >= 32 ? SHIL_LARGE : SHIL_SMALL);
            if (FAILED(SHGetImageList(listSize, IID_PPV_ARGS(&imageList))) || imageList == nullptr)
                return nullptr;
            HICON icon = nullptr;
            imageList->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon);
            imageList->Release();
            return icon;
        }

        std::wstring DefaultBrowserExecutable()
        {
            DWORD length = 0;
            if (AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"http", L"open", nullptr, &length) != S_FALSE || length == 0)
                return {};
            std::wstring path(length, L'\0');
            if (FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"http", L"open", path.data(), &length)))
                return {};
            if (!path.empty() && path.back() == L'\0')
                path.pop_back();
            return path;
        }

        ShellIconResult FallbackIcon(int requestedSize)
        {
            HICON shared = static_cast<HICON>(LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON,
                requestedSize, requestedSize, LR_SHARED));
            return { shared != nullptr ? CopyIcon(shared) : nullptr, true, {} };
        }
    }

    bool ResolveShortcut(const std::wstring& shortcutPath, ShortcutInfo& result)
    {
        ComPtr<IShellLinkW> shellLink;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink))))
            return false;

        ComPtr<IPersistFile> persistFile;
        if (FAILED(shellLink.As(&persistFile)) || FAILED(persistFile->Load(shortcutPath.c_str(), STGM_READ)))
            return false;

        std::wstring target(32768, L'\0');
        WIN32_FIND_DATAW findData{};
        if (SUCCEEDED(shellLink->GetPath(target.data(), static_cast<int>(target.size()), &findData, 0)))
            target.resize(wcsnlen_s(target.c_str(), target.size()));
        else
            target.clear();

        std::wstring iconPath(32768, L'\0');
        int iconIndex = 0;
        if (SUCCEEDED(shellLink->GetIconLocation(iconPath.data(), static_cast<int>(iconPath.size()), &iconIndex)))
            iconPath.resize(wcsnlen_s(iconPath.c_str(), iconPath.size()));
        else
            iconPath.clear();

        std::wstring arguments(32768, L'\0');
        if (SUCCEEDED(shellLink->GetArguments(arguments.data(), static_cast<int>(arguments.size()))))
            arguments.resize(wcsnlen_s(arguments.c_str(), arguments.size()));
        else
            arguments.clear();

        std::wstring workingDirectory(32768, L'\0');
        if (SUCCEEDED(shellLink->GetWorkingDirectory(workingDirectory.data(), static_cast<int>(workingDirectory.size()))))
            workingDirectory.resize(wcsnlen_s(workingDirectory.c_str(), workingDirectory.size()));
        else
            workingDirectory.clear();

        result = {
            std::move(target),
            std::move(arguments),
            std::move(workingDirectory),
            std::move(iconPath),
            iconIndex
        };
        return !result.targetPath.empty() || !result.iconPath.empty();
    }

    ShellIconResult LoadShellIcon(const LaunchItem& item, int requestedSize)
    {
        const std::wstring customIcon = Utf8ToWide(ResolvePortablePath(item.customIconPath));
        if (!customIcon.empty())
        {
            if (HICON icon = ExtractIcon(customIcon, 0, requestedSize))
                return { icon, false, customIcon };
        }

        const std::wstring target = Utf8ToWide(ResolvePortablePath(item.target));
        if (item.type == ItemType::Shell)
        {
            if (HICON icon = IconFromShellParsingName(target, requestedSize))
                return { icon, false, target };
            return FallbackIcon(requestedSize);
        }
        if (item.type == ItemType::Url)
        {
            const std::wstring browser = DefaultBrowserExecutable();
            if (HICON icon = ExtractIcon(browser, 0, requestedSize))
                return { icon, false, browser };
            return FallbackIcon(requestedSize);
        }

        if (item.type == ItemType::Shortcut)
        {
            ShortcutInfo shortcut;
            if (ResolveShortcut(target, shortcut))
            {
                if (HICON icon = ExtractIcon(shortcut.iconPath, shortcut.iconIndex, requestedSize))
                    return { icon, false, shortcut.iconPath };
                if (!shortcut.targetPath.empty())
                {
                    std::error_code error;
                    const bool exists = std::filesystem::exists(shortcut.targetPath, error);
                    const bool folder = std::filesystem::is_directory(shortcut.targetPath, error);
                    if (HICON icon = IconFromShell(
                        shortcut.targetPath,
                        folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
                        !exists, requestedSize))
                        return { icon, false, shortcut.targetPath };
                }
            }
        }

        std::error_code error;
        const bool exists = std::filesystem::exists(target, error);
        const bool folder = item.type == ItemType::Folder || std::filesystem::is_directory(target, error);
        if (HICON icon = IconFromShell(
            target,
            folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
            !exists, requestedSize))
            return { icon, false, target };
        return FallbackIcon(requestedSize);
    }
}
