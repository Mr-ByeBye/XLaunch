#include "platform/PortablePath.h"

#include <windows.h>

namespace xlaunch
{
    namespace
    {
        std::wstring Utf8ToWide(const std::string& value)
        {
            if (value.empty()) return {};
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (size <= 0) return {};
            std::wstring result(size, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
            return result;
        }

        std::string WideToUtf8(const std::wstring& value)
        {
            if (value.empty()) return {};
            const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string result(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
            return result;
        }

        bool IsUrl(const std::string& value)
        {
            return value.find("://") != std::string::npos;
        }

        bool EscapesBase(const std::filesystem::path& relative)
        {
            if (relative.empty() || relative.is_absolute()) return true;
            const auto first = relative.begin();
            return first != relative.end() && *first == L"..";
        }
    }

    std::filesystem::path ExecutableDirectory()
    {
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) return std::filesystem::current_path();
        path.resize(length);
        return std::filesystem::path(path).parent_path();
    }

    std::string ResolvePortablePath(const std::string& value)
    {
        if (value.empty() || IsUrl(value)) return value;
        const std::filesystem::path path(Utf8ToWide(value));
        if (path.is_absolute()) return value;
        return WideToUtf8((ExecutableDirectory() / path).lexically_normal().wstring());
    }

    std::string MakePortablePath(const std::string& value)
    {
        if (value.empty() || IsUrl(value)) return value;
        const std::filesystem::path input(Utf8ToWide(value));
        if (!input.is_absolute()) return WideToUtf8(input.lexically_normal().wstring());

        std::error_code error;
        const std::filesystem::path base = std::filesystem::weakly_canonical(ExecutableDirectory(), error);
        if (error) return value;
        const std::filesystem::path candidate = std::filesystem::weakly_canonical(input, error);
        if (error) return value;
        const std::filesystem::path relative = std::filesystem::relative(candidate, base, error);
        if (error || EscapesBase(relative)) return value;
        return WideToUtf8(relative.lexically_normal().wstring());
    }

    bool MakeLaunchItemPortable(LaunchItem& item)
    {
        const std::string target = MakePortablePath(item.target);
        const std::string workingDirectory = MakePortablePath(item.workingDirectory);
        const std::string icon = MakePortablePath(item.customIconPath);
        const bool changed = target != item.target || workingDirectory != item.workingDirectory || icon != item.customIconPath;
        item.target = target;
        item.workingDirectory = workingDirectory;
        item.customIconPath = icon;
        return changed;
    }
}
