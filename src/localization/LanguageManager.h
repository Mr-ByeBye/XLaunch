#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace xlaunch
{
    class LanguageManager
    {
    public:
        static void Initialize(const std::filesystem::path& configDirectory, const std::string& language);
        static bool SetLanguage(const std::string& language);
        [[nodiscard]] static const char* Get(const char* key);
        [[nodiscard]] static std::wstring GetWide(const char* key);
        [[nodiscard]] static const std::vector<std::string>& AvailableLanguages();
        [[nodiscard]] static const std::string& CurrentLanguage();
        [[nodiscard]] static std::string DetectSystemLanguage();
    };
}
