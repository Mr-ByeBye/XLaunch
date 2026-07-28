#pragma once

#include "core/LauncherData.h"

#include <string>

namespace xlaunch
{
    struct LaunchItemResult
    {
        bool success = false;
        LaunchItem item;
        std::string error;
    };

    [[nodiscard]] LaunchItemResult CreateLaunchItemFromPath(const std::wstring& path);
    [[nodiscard]] LaunchItemResult CreateLaunchItemFromShellItem(
        const std::wstring& fileSystemPath,
        const std::wstring& parsingName,
        const std::wstring& displayName);
}
