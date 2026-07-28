#include "platform/LaunchItemFactory.h"

#include "platform/ShellIcon.h"
#include "platform/PortablePath.h"

#include <algorithm>
#include <filesystem>
#include <vector>
#include <windows.h>

namespace xlaunch
{
    namespace
    {
        std::string WideToUtf8(const std::wstring& value)
        {
            if (value.empty())
                return {};
            const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string result(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
            return result;
        }

        std::string PathName(const std::filesystem::path& path, bool removeExtension)
        {
            const auto value = (removeExtension ? path.stem() : path.filename()).u8string();
            return std::string(value.begin(), value.end());
        }

        std::string FileDescription(const std::filesystem::path& path)
        {
            DWORD ignored = 0;
            const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
            if (size == 0)
                return {};
            std::vector<std::byte> data(size);
            if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
                return {};

            struct Translation { WORD language; WORD codePage; };
            Translation* translations = nullptr;
            UINT translationBytes = 0;
            if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &translationBytes) || translationBytes < sizeof(Translation))
                return {};

            for (UINT index = 0; index < translationBytes / sizeof(Translation); ++index)
            {
                wchar_t query[128]{};
                swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[index].language, translations[index].codePage);
                wchar_t* description = nullptr;
                UINT descriptionLength = 0;
                if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&description), &descriptionLength) && descriptionLength > 1)
                    return WideToUtf8(description);
            }
            return {};
        }

        std::wstring ReadInternetShortcut(const std::filesystem::path& path, const wchar_t* key)
        {
            std::wstring value(32768, L'\0');
            const DWORD length = GetPrivateProfileStringW(L"InternetShortcut", key, L"", value.data(), static_cast<DWORD>(value.size()), path.c_str());
            value.resize(length);
            return value;
        }
    }

    LaunchItemResult CreateLaunchItemFromPath(const std::wstring& rawPath)
    {
        if (rawPath.empty())
            return { false, {}, "路径为空。" };

        std::error_code error;
        const std::filesystem::path path = std::filesystem::absolute(rawPath, error);
        if (error || !std::filesystem::exists(path, error))
            return { false, {}, "目标不存在：" + WideToUtf8(rawPath) };

        LaunchItem item;
        item.id = MakeId("item");
        item.sortOrder = 0;

        if (std::filesystem::is_directory(path, error))
        {
            item.type = ItemType::Folder;
            item.target = WideToUtf8(path.wstring());
            item.automaticName = PathName(path, false);
            if (item.automaticName.empty())
                item.automaticName = item.target;
            item.workingDirectory = item.target;
            MakeLaunchItemPortable(item);
            return { true, std::move(item), {} };
        }

        std::wstring extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (extension == L".lnk")
        {
            ShortcutInfo shortcut;
            if (!ResolveShortcut(path.wstring(), shortcut) || shortcut.targetPath.empty())
                return { false, {}, "无法解析快捷方式：" + WideToUtf8(path.wstring()) };
            item.type = ItemType::Shortcut;
            item.target = WideToUtf8(shortcut.targetPath);
            item.arguments = WideToUtf8(shortcut.arguments);
            item.workingDirectory = WideToUtf8(shortcut.workingDirectory);
            if (item.workingDirectory.empty())
                item.workingDirectory = WideToUtf8(std::filesystem::path(shortcut.targetPath).parent_path().wstring());
            std::filesystem::path iconPath(shortcut.iconPath);
            if (!iconPath.empty() && iconPath.is_relative())
                iconPath = path.parent_path() / iconPath;
            item.customIconPath = WideToUtf8(iconPath.wstring());
            item.automaticName = PathName(path, true);
            MakeLaunchItemPortable(item);
            return { true, std::move(item), {} };
        }

        if (extension == L".url")
        {
            const std::wstring url = ReadInternetShortcut(path, L"URL");
            if (url.empty())
                return { false, {}, "无法从 Internet 快捷方式读取 URL：" + WideToUtf8(path.wstring()) };
            item.type = ItemType::Url;
            item.target = WideToUtf8(url);
            item.automaticName = PathName(path, true);
            item.customIconPath = WideToUtf8(ReadInternetShortcut(path, L"IconFile"));
            MakeLaunchItemPortable(item);
            return { true, std::move(item), {} };
        }

        item.target = WideToUtf8(path.wstring());
        if (extension == L".exe")
        {
            item.type = ItemType::Executable;
            item.automaticName = FileDescription(path);
            if (item.automaticName.empty())
                item.automaticName = PathName(path, true);
            item.workingDirectory = WideToUtf8(path.parent_path().wstring());
        }
        else
        {
            item.type = ItemType::File;
            item.automaticName = PathName(path, false);
            item.workingDirectory = WideToUtf8(path.parent_path().wstring());
        }
        MakeLaunchItemPortable(item);
        return { true, std::move(item), {} };
    }

    LaunchItemResult CreateLaunchItemFromShellItem(
        const std::wstring& fileSystemPath,
        const std::wstring& parsingName,
        const std::wstring& displayName)
    {
        if (!fileSystemPath.empty())
        {
            LaunchItemResult fileResult = CreateLaunchItemFromPath(fileSystemPath);
            if (fileResult.success)
            {
                if (!displayName.empty() && fileResult.item.automaticName.empty())
                    fileResult.item.automaticName = WideToUtf8(displayName);
                return fileResult;
            }
        }

        const std::wstring& target = !parsingName.empty() ? parsingName : fileSystemPath;
        if (target.empty())
            return { false, {}, "无法读取该 Windows Shell 项目的目标。" };

        LaunchItem item;
        item.id = MakeId("item");
        item.type = ItemType::Shell;
        item.target = WideToUtf8(target);
        item.automaticName = WideToUtf8(displayName);
        if (item.automaticName.empty())
            item.automaticName = DeriveAutomaticName(item.target);
        return { true, std::move(item), {} };
    }
}
