#include "platform/ShellIcon.h"
#include "platform/PortablePath.h"

#include <algorithm>
#include <cstdint>
#include <cwctype>
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
            // Avoid asking Windows for an in-between size. On Windows 11 that can
            // select a 32px resource and enlarge it for 40px, producing blocky icons.
            const int sourceSize = requestedSize <= 32 ? 32 : (requestedSize <= 48 ? 48 : 256);
            return PrivateExtractIconsW(path.c_str(), iconIndex, sourceSize, sourceSize, &icon, &identifier, 1, LR_LOADFROMFILE) > 0
                ? icon : nullptr;
        }

        bool HasDirectIconResource(const std::wstring& path)
        {
            const std::filesystem::path file(path);
            std::wstring extension = file.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
            return extension == L".exe" || extension == L".dll" || extension == L".ico" || extension == L".icl";
        }

        int ShellImageListSize(int requestedSize)
        {
            if (requestedSize <= 32)
                return SHIL_LARGE;
            if (requestedSize <= 48)
                return SHIL_EXTRALARGE;
            return SHIL_JUMBO;
        }

        HICON ImageListIcon(int listSize, int iconIndex)
        {
            IImageList* imageList = nullptr;
            if (FAILED(SHGetImageList(listSize, IID_PPV_ARGS(&imageList))) || imageList == nullptr)
                return nullptr;
            HICON icon = nullptr;
            imageList->GetIcon(iconIndex, ILD_TRANSPARENT, &icon);
            imageList->Release();
            return icon;
        }

        std::size_t VisiblePixelCount(HICON icon)
        {
            if (icon == nullptr) return 0;
            constexpr int size = 64;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = size;
            info.bmiHeader.biHeight = -size;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            HDC context = CreateCompatibleDC(nullptr);
            if (bitmap == nullptr || context == nullptr || bits == nullptr)
            {
                if (context) DeleteDC(context);
                if (bitmap) DeleteObject(bitmap);
                return 0;
            }
            const HGDIOBJ old = SelectObject(context, bitmap);
            std::fill_n(static_cast<std::uint32_t*>(bits), size * size, 0u);
            DrawIconEx(context, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
            const auto* pixels = static_cast<const std::uint8_t*>(bits);
            std::size_t visible = 0;
            for (int index = 0; index < size * size; ++index)
                visible += pixels[index * 4] != 0 || pixels[index * 4 + 1] != 0 ||
                    pixels[index * 4 + 2] != 0 || pixels[index * 4 + 3] != 0;
            SelectObject(context, old);
            DeleteDC(context);
            DeleteObject(bitmap);
            return visible;
        }

        HICON BestShellIcon(int iconIndex, int requestedSize)
        {
            if (requestedSize <= 48)
                return ImageListIcon(ShellImageListSize(requestedSize), iconIndex);

            HICON jumbo = ImageListIcon(SHIL_JUMBO, iconIndex);
            HICON extraLarge = ImageListIcon(SHIL_EXTRALARGE, iconIndex);
            const std::size_t jumboPixels = VisiblePixelCount(jumbo);
            const std::size_t extraLargePixels = VisiblePixelCount(extraLarge);

            // Some Windows 11 Jumbo entries contain only a tiny shortcut overlay.
            // Prefer the complete 48px icon when Jumbo has substantially less content.
            if (jumbo == nullptr || (extraLargePixels > 0 && jumboPixels * 4 < extraLargePixels))
            {
                if (jumbo) DestroyIcon(jumbo);
                return extraLarge;
            }
            if (extraLarge) DestroyIcon(extraLarge);
            return jumbo;
        }

        HICON IconFromShell(const std::wstring& path, DWORD fileAttributes, bool useFileAttributes, int requestedSize)
        {
            SHFILEINFOW info{};
            UINT flags = SHGFI_SYSICONINDEX;
            if (useFileAttributes)
                flags |= SHGFI_USEFILEATTRIBUTES;
            if (SHGetFileInfoW(path.c_str(), fileAttributes, &info, sizeof(info), flags) == 0)
                return nullptr;
            return BestShellIcon(info.iIcon, requestedSize);
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
            return BestShellIcon(info.iIcon, requestedSize);
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
                    if (!folder && HasDirectIconResource(shortcut.targetPath))
                        if (HICON icon = ExtractIcon(shortcut.targetPath, 0, requestedSize))
                            return { icon, false, shortcut.targetPath };
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
        if (!folder && (item.type == ItemType::Executable || HasDirectIconResource(target)))
            if (HICON icon = ExtractIcon(target, 0, requestedSize))
                return { icon, false, target };
        if (HICON icon = IconFromShell(
            target,
            folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
            !exists, requestedSize))
            return { icon, false, target };
        return FallbackIcon(requestedSize);
    }
}
