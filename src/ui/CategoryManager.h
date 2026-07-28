#pragma once

#include "core/LauncherData.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <windows.h>

namespace xlaunch
{
    struct ItemDragPayload
    {
        std::size_t sourceCategory = 0;
        std::size_t itemIndex = 0;
    };

    struct ItemMoveRequest
    {
        bool requested = false;
        ItemDragPayload source;
        std::size_t destinationCategory = 0;
    };

    class CategoryManager
    {
    public:
        void OpenAdd() { addRequested_ = true; }
        void Draw(HWND owner, AppConfig& config, std::size_t& selectedCategory, bool& changed,
            ItemMoveRequest& itemMove, bool externalDrag, std::size_t externalTargetCategory,
            float width, float dpiScale);
        [[nodiscard]] std::size_t HitTestCategory(POINTL screenPoint, std::size_t fallback) const;

    private:
        [[nodiscard]] bool NameExists(const AppConfig& config, const std::string& name, std::size_t ignoredIndex) const;
        void DeleteCategory(AppConfig& config, std::size_t index, std::size_t& selectedCategory);

        bool addRequested_ = false;
        int renameIndex_ = -1;
        int deleteIndex_ = -1;
        int moveDestination_ = -1;
        bool focusRenameInput_ = false;
        std::array<char, 128> nameBuffer_{};
        std::string validationError_;
        std::vector<RECT> categoryRects_;
    };
}
