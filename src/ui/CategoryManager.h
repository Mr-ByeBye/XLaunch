#pragma once

#include "core/LauncherData.h"

#include <array>
#include <cstddef>
#include <string>

namespace xlaunch
{
    class CategoryManager
    {
    public:
        void OpenAdd() { addRequested_ = true; }
        void Draw(AppConfig& config, std::size_t& selectedCategory, bool& changed, float width, float dpiScale);

    private:
        [[nodiscard]] bool NameExists(const AppConfig& config, const std::string& name, std::size_t ignoredIndex) const;
        void DeleteCategory(AppConfig& config, std::size_t index, std::size_t& selectedCategory);

        bool addRequested_ = false;
        int renameIndex_ = -1;
        int deleteIndex_ = -1;
        int moveDestination_ = -1;
        std::array<char, 128> nameBuffer_{};
        std::string validationError_;
    };
}
