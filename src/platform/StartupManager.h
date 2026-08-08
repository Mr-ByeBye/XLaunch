#pragma once

#include <string>

#include "core/LauncherData.h"

namespace xlaunch
{
    class StartupManager
    {
    public:
        [[nodiscard]] static bool SetEnabled(bool enabled, StartupPriority priority, std::string& error);
        [[nodiscard]] static bool IsEnabled();
    };
}
