#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <windows.h>

namespace xlaunch
{
    class BackupManager
    {
    public:
        [[nodiscard]] static std::optional<std::filesystem::path> ChooseExportPath(HWND owner);
        [[nodiscard]] static std::optional<std::filesystem::path> ChooseImportPath(HWND owner);
        [[nodiscard]] static bool Export(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& error);
        [[nodiscard]] static bool CreateAutomatic(const std::filesystem::path& source, int keepCount, bool force, std::string& error);
    };
}
