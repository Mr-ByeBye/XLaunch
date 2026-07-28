#pragma once

#include "core/LauncherData.h"

#include <filesystem>
#include <string>

namespace xlaunch
{
    [[nodiscard]] std::filesystem::path ExecutableDirectory();
    [[nodiscard]] std::string ResolvePortablePath(const std::string& value);
    [[nodiscard]] std::string MakePortablePath(const std::string& value);
    bool MakeLaunchItemPortable(LaunchItem& item);
}
