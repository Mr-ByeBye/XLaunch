#pragma once

#include <string>

namespace xlaunch
{
    class StartupManager
    {
    public:
        [[nodiscard]] static bool SetEnabled(bool enabled, std::string& error);
        [[nodiscard]] static bool IsEnabled();
    };
}
