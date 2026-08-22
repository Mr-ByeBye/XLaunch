#include "platform/LaunchOperations.h"
#include "platform/PortablePath.h"

#include <commdlg.h>
#include <filesystem>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <vector>

namespace xlaunch
{
    namespace
    {
        std::wstring Utf8ToWide(const std::string& value)
        {
            if (value.empty())
                return {};
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (size <= 0)
                return {};
            std::wstring result(size, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
            return result;
        }
        std::string WideToUtf8(const std::wstring& value)
        {
            if (value.empty())
                return {};
            const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string result(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
            return result;
        }

        std::string FormatWindowsError(DWORD code)
        {
            wchar_t* buffer = nullptr;
            const DWORD length = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
            std::string message = "错误代码 " + std::to_string(code);
            if (length != 0 && buffer != nullptr)
            {
                message = WideToUtf8(std::wstring(buffer, length));
                while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
                    message.pop_back();
            }
            if (buffer != nullptr)
                LocalFree(buffer);
            return message;
        }

        OperationResult ShellOpen(
            const wchar_t* verb,
            const std::wstring& target,
            const std::wstring& parameters,
            const std::wstring& workingDirectory)
        {
            if (target.empty())
                return { false, "目标路径为空。" };

            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            // XLaunch owns a persistent UI message loop, so the Shell may finish
            // DDE or execution-delegate activation in the background. Forcing
            // SEE_MASK_NOASYNC here can stall the render thread during handoff.
            // Keep Windows in charge of trust and error prompts (including
            // SmartScreen) and make the active XLaunch window their owner, so a
            // first-run security confirmation is visible instead of silent.
            info.fMask = SEE_MASK_ASYNCOK;
            info.hwnd = GetActiveWindow();
            info.lpVerb = verb;
            info.lpFile = target.c_str();
            info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
            info.lpDirectory = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
            info.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&info))
                return { false, FormatWindowsError(GetLastError()) };
            return { true, {} };
        }

        OperationResult ShellOpenParsingName(const std::wstring& target)
        {
            PIDLIST_ABSOLUTE itemIdList = nullptr;
            const HRESULT parseResult = SHParseDisplayName(target.c_str(), nullptr, &itemIdList, 0, nullptr);
            if (FAILED(parseResult) || itemIdList == nullptr)
                return ShellOpen(L"open", target, {}, {});

            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_NO_UI | SEE_MASK_ASYNCOK;
            info.lpVerb = L"open";
            info.lpIDList = itemIdList;
            info.nShow = SW_SHOWNORMAL;
            const BOOL launched = ShellExecuteExW(&info);
            const DWORD error = launched ? ERROR_SUCCESS : GetLastError();
            CoTaskMemFree(itemIdList);
            return launched ? OperationResult{ true, {} } : OperationResult{ false, FormatWindowsError(error) };
        }

    }

    OperationResult Launch(const LaunchItem& item, bool forceAdministrator)
    {
        if (item.type == ItemType::Shell)
            return ShellOpenParsingName(Utf8ToWide(item.target));
        const bool administrator = forceAdministrator || item.runAsAdministrator;
        return ShellOpen(
            administrator ? L"runas" : L"open",
            Utf8ToWide(ResolvePortablePath(item.target)),
            Utf8ToWide(item.arguments),
            Utf8ToWide(ResolvePortablePath(item.workingDirectory)));
    }

    OperationResult OpenContainingLocation(const LaunchItem& item)
    {
        if (item.type == ItemType::Shell)
            return { false, "Windows Shell 项目没有可打开的本地所在位置。" };
        const std::wstring target = Utf8ToWide(ResolvePortablePath(item.target));
        if (target.empty() || item.target.find("://") != std::string::npos)
            return { false, "该目标没有可打开的本地位置。" };

        const std::filesystem::path path(target);
        std::error_code errorCode;
        if (std::filesystem::is_directory(path, errorCode))
            return ShellOpen(L"open", path.wstring(), {}, {});

        if (!std::filesystem::exists(path, errorCode))
            return { false, "目标不存在，无法打开所在位置。" };

        return ShellOpen(L"open", L"explorer.exe", L"/select,\"" + path.wstring() + L"\"", {});
    }

    std::optional<std::string> BrowseForTarget(HWND owner)
    {
        std::vector<wchar_t> buffer(32768, L'\0');
        constexpr wchar_t filter[] =
            L"支持的目标\0*.exe;*.lnk;*.*\0"
            L"可执行文件 (*.exe)\0*.exe\0"
            L"快捷方式 (*.lnk)\0*.lnk\0"
            L"所有文件 (*.*)\0*.*\0\0";

        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        dialog.lpstrTitle = L"选择启动目标";
        if (!GetOpenFileNameW(&dialog))
            return std::nullopt;
        return WideToUtf8(buffer.data());
    }

    std::optional<std::string> BrowseForFolder(HWND owner)
    {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            return std::nullopt;
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"选择启动文件夹");
        if (FAILED(dialog->Show(owner)))
            return std::nullopt;
        Microsoft::WRL::ComPtr<IShellItem> item;
        if (FAILED(dialog->GetResult(&item)))
            return std::nullopt;
        PWSTR path = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            return std::nullopt;
        const std::string result = WideToUtf8(path);
        CoTaskMemFree(path);
        return result;
    }
}
