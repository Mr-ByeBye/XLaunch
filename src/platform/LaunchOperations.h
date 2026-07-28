#pragma once

#include "core/LauncherData.h"

#include <optional>
#include <string>

#include <windows.h>

namespace xlaunch
{
    struct OperationResult
    {
        bool success = false;
        std::string error;
    };

    [[nodiscard]] OperationResult Launch(const LaunchItem& item, bool forceAdministrator = false);
    [[nodiscard]] OperationResult OpenContainingLocation(const LaunchItem& item);
    [[nodiscard]] std::optional<std::string> BrowseForTarget(HWND owner);
    [[nodiscard]] std::optional<std::string> BrowseForFolder(HWND owner);
}
