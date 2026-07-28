#pragma once

#include "core/LauncherData.h"

#include <filesystem>
#include <string>

namespace xlaunch
{
    class ConfigManager
    {
    public:
        struct LoadResult
        {
            AppConfig config;
            std::string error;
        };

        explicit ConfigManager(std::filesystem::path configPath = DefaultConfigPath());

        [[nodiscard]] LoadResult Load() const;
        [[nodiscard]] bool Save(const AppConfig& config, std::string& error) const;
        [[nodiscard]] const std::filesystem::path& Path() const { return configPath_; }

        [[nodiscard]] static std::filesystem::path DefaultConfigPath();

    private:
        std::filesystem::path configPath_;
        mutable bool sourceWasCorrupt_ = false;
    };
}
