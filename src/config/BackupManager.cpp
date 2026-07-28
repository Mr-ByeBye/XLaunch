#include "config/BackupManager.h"

#include <algorithm>
#include <chrono>
#include <commdlg.h>
#include <fstream>
#include <vector>

namespace xlaunch
{
    namespace
    {
        std::optional<std::filesystem::path> ChooseJson(HWND owner, bool save)
        {
            wchar_t path[32768]{};
            OPENFILENAMEW dialog{ sizeof(OPENFILENAMEW) };
            dialog.hwndOwner = owner;
            dialog.lpstrFilter = L"XLaunch 配置 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
            dialog.lpstrFile = path;
            dialog.nMaxFile = static_cast<DWORD>(std::size(path));
            dialog.lpstrDefExt = L"json";
            dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
            const BOOL result = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
            return result ? std::optional<std::filesystem::path>(path) : std::nullopt;
        }
    }

    std::optional<std::filesystem::path> BackupManager::ChooseExportPath(HWND owner) { return ChooseJson(owner, true); }
    std::optional<std::filesystem::path> BackupManager::ChooseImportPath(HWND owner) { return ChooseJson(owner, false); }

    bool BackupManager::Export(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& error)
    {
        std::error_code copyError;
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, copyError);
        if (copyError)
        {
            error = "备份配置失败：" + copyError.message();
            return false;
        }
        error.clear();
        return true;
    }

    bool BackupManager::CreateAutomatic(const std::filesystem::path& source, int keepCount, bool force, std::string& error)
    {
        if (!std::filesystem::exists(source))
            return true;
        const std::filesystem::path directory = source.parent_path() / L"backups";
        std::error_code fileError;
        std::filesystem::create_directories(directory, fileError);
        if (fileError)
        {
            error = "无法创建自动备份目录：" + fileError.message();
            return false;
        }

        std::vector<std::filesystem::directory_entry> files;
        for (const auto& entry : std::filesystem::directory_iterator(directory, fileError))
            if (entry.is_regular_file() && entry.path().extension() == L".json") files.push_back(entry);
        if (fileError) return true;
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.last_write_time() > b.last_write_time(); });
        if (!force && !files.empty() && std::filesystem::file_time_type::clock::now() - files.front().last_write_time() < std::chrono::minutes(10))
            return true;

        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (!Export(source, directory / (L"xlaunch-" + std::to_wstring(stamp) + L".json"), error))
            return false;

        files.clear();
        for (const auto& entry : std::filesystem::directory_iterator(directory, fileError))
            if (entry.is_regular_file() && entry.path().extension() == L".json") files.push_back(entry);
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.last_write_time() > b.last_write_time(); });
        for (std::size_t index = static_cast<std::size_t>((std::max)(1, keepCount)); index < files.size(); ++index)
            std::filesystem::remove(files[index].path(), fileError);
        error.clear();
        return true;
    }
}
