#include "platform/StartupManager.h"

#include <windows.h>

namespace xlaunch
{
    namespace
    {
        constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        constexpr wchar_t kValueName[] = L"XLaunch";
    }

    bool StartupManager::SetEnabled(bool enabled, std::string& error)
    {
        HKEY key = nullptr;
        const LSTATUS openResult = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (openResult != ERROR_SUCCESS)
        {
            error = "无法打开当前用户启动项，错误代码：" + std::to_string(openResult);
            return false;
        }

        LSTATUS result = ERROR_SUCCESS;
        if (enabled)
        {
            std::wstring path(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0 || length >= path.size())
                result = GetLastError();
            else
            {
                path.resize(length);
                const std::wstring command = L"\"" + path + L"\" --startup";
                result = RegSetValueExW(key, kValueName, 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
            }
        }
        else
        {
            result = RegDeleteValueW(key, kValueName);
            if (result == ERROR_FILE_NOT_FOUND)
                result = ERROR_SUCCESS;
        }
        RegCloseKey(key);
        if (result != ERROR_SUCCESS)
        {
            error = "更新开机自启失败，错误代码：" + std::to_string(result);
            return false;
        }
        error.clear();
        return true;
    }

    bool StartupManager::IsEnabled()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
            return false;
        const LSTATUS result = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    }
}
