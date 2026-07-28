#pragma once

#include "core/LauncherData.h"

#include <string>

#include <windows.h>

namespace xlaunch
{
    struct ShortcutInfo
    {
        std::wstring targetPath;
        std::wstring arguments;
        std::wstring workingDirectory;
        std::wstring iconPath;
        int iconIndex = 0;
    };

    struct ShellIconResult
    {
        HICON icon = nullptr;
        bool usedFallback = false;
        std::wstring sourcePath;
    };

    [[nodiscard]] bool ResolveShortcut(const std::wstring& shortcutPath, ShortcutInfo& result);
    [[nodiscard]] ShellIconResult LoadShellIcon(const LaunchItem& item, int requestedSize = 32);
}
