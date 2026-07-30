#include "ui/ItemEditor.h"

#include "platform/LaunchOperations.h"
#include "platform/LaunchItemFactory.h"
#include "platform/PortablePath.h"
#include "renderer/IconCache.h"

#include <cstring>

#include "imgui.h"

namespace xlaunch
{
    namespace
    {
        template <std::size_t Size>
        void CopyText(std::array<char, Size>& destination, const std::string& source)
        {
            strncpy_s(destination.data(), destination.size(), source.c_str(), _TRUNCATE);
        }

        void DrawField(const char* label, char* buffer, std::size_t size)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(130.0f);
            ImGui::SetNextItemWidth(430.0f);
            ImGui::InputText((std::string("##") + label).c_str(), buffer, size);
        }

        std::wstring Utf8ToWide(const std::string& value)
        {
            if (value.empty())
                return {};
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
            std::wstring result(size, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
            return result;
        }

        bool IsModifierKey(int key)
        {
            return key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
                key == VK_MENU || key == VK_LMENU || key == VK_RMENU ||
                key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
                key == VK_LWIN || key == VK_RWIN;
        }

        std::string KeyName(int virtualKey)
        {
            if (virtualKey == 0) return "未设置";
            if (virtualKey >= 'A' && virtualKey <= 'Z') return std::string(1, static_cast<char>(virtualKey));
            if (virtualKey >= '0' && virtualKey <= '9') return std::string(1, static_cast<char>(virtualKey));
            if (virtualKey >= VK_F1 && virtualKey <= VK_F24) return "F" + std::to_string(virtualKey - VK_F1 + 1);
            if (virtualKey == VK_SPACE) return "Space";
            if (virtualKey == VK_RETURN) return "Enter";
            if (virtualKey == VK_TAB) return "Tab";
            if (virtualKey == VK_DELETE) return "Delete";
            const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
            wchar_t name[64]{};
            if (GetKeyNameTextW(static_cast<LONG>(scanCode << 16), name, static_cast<int>(std::size(name))) > 0)
            {
                const int size = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
                std::string result(static_cast<std::size_t>(size), '\0');
                WideCharToMultiByte(CP_UTF8, 0, name, -1, result.data(), size, nullptr, nullptr);
                result.resize(static_cast<std::size_t>(size - 1));
                return result;
            }
            return "VK " + std::to_string(virtualKey);
        }

        std::string ShortcutText(const LaunchItem::Shortcut& shortcut)
        {
            if (!shortcut.enabled || shortcut.virtualKey == 0) return "未设置";
            std::string result;
            auto append = [&](const char* text) { if (!result.empty()) result += " + "; result += text; };
            if ((shortcut.modifiers & HotkeyControl) != 0) append("Ctrl");
            if ((shortcut.modifiers & HotkeyAlt) != 0) append("Alt");
            if ((shortcut.modifiers & HotkeyShift) != 0) append("Shift");
            if ((shortcut.modifiers & HotkeyWin) != 0) append("Win");
            append(KeyName(shortcut.virtualKey).c_str());
            return result;
        }
    }

    void ItemEditor::OpenNew(std::size_t categoryIndex)
    {
        open_ = true;
        editing_ = false;
        categoryIndex_ = categoryIndex;
        itemIndex_ = 0;
        existingId_.clear();
        automaticName_.clear();
        type_ = ItemType::File;
        validationError_.clear();
        customName_.fill('\0');
        target_.fill('\0');
        arguments_.fill('\0');
        workingDirectory_.fill('\0');
        customIconPath_.fill('\0');
        runAsAdministrator_ = false;
        globalShortcut_ = {};
        localShortcut_ = {};
        capturingShortcut_ = 0;
        openRequested_ = true;
    }

    void ItemEditor::OpenEdit(const AppConfig& config, std::size_t categoryIndex, std::size_t itemIndex)
    {
        if (categoryIndex >= config.categories.size() || itemIndex >= config.categories[categoryIndex].items.size())
            return;
        open_ = true;
        editing_ = true;
        categoryIndex_ = categoryIndex;
        itemIndex_ = itemIndex;
        validationError_.clear();
        FillFrom(config.categories[categoryIndex].items[itemIndex]);
        openRequested_ = true;
    }

    void ItemEditor::FillFrom(const LaunchItem& item)
    {
        existingId_ = item.id;
        automaticName_ = item.automaticName;
        type_ = item.type;
        CopyText(customName_, item.customName);
        CopyText(target_, item.target);
        CopyText(arguments_, item.arguments);
        CopyText(workingDirectory_, item.workingDirectory);
        CopyText(customIconPath_, item.customIconPath);
        runAsAdministrator_ = item.runAsAdministrator;
        globalShortcut_ = item.globalShortcut;
        localShortcut_ = item.localShortcut;
        capturingShortcut_ = 0;
    }

    void ItemEditor::ApplySelectedPath(const std::string& path)
    {
        LaunchItemResult result = CreateLaunchItemFromPath(Utf8ToWide(path));
        if (!result.success)
        {
            validationError_ = std::move(result.error);
            return;
        }
        automaticName_ = result.item.automaticName;
        type_ = result.item.type;
        CopyText(target_, result.item.target);
        CopyText(arguments_, result.item.arguments);
        CopyText(workingDirectory_, result.item.workingDirectory);
        CopyText(customIconPath_, result.item.customIconPath);
        validationError_.clear();
    }

    LaunchItem ItemEditor::BuildItem() const
    {
        LaunchItem item;
        item.id = existingId_.empty() ? "item-editor-preview" : existingId_;
        item.customName = customName_.data();
        item.target = target_.data();
        item.automaticName = automaticName_.empty() ? DeriveAutomaticName(item.target) : automaticName_;
        item.arguments = arguments_.data();
        item.workingDirectory = workingDirectory_.data();
        item.customIconPath = customIconPath_.data();
        item.runAsAdministrator = runAsAdministrator_;
        item.type = type_;
        item.globalShortcut = globalShortcut_;
        item.localShortcut = localShortcut_;
        return item;
    }

    void ItemEditor::Draw(HWND owner, AppConfig& config, IconCache& iconCache, bool& changed, bool& saveImmediately)
    {
        if (openRequested_)
        {
            openRequested_ = false;
        }
        if (!open_)
            return;

        const char* title = editing_ ? "编辑启动项目" : "新增启动项目";
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        const std::string windowTitle = std::string(title) + "###ItemEditorTool";
        if (!ImGui::Begin(windowTitle.c_str(), &open_, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::End();
            return;
        }

        DrawField("自定义名称", customName_.data(), customName_.size());

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("目标路径");
        ImGui::SameLine(130.0f);
        ImGui::SetNextItemWidth(345.0f);
        if (ImGui::InputText("##目标路径", target_.data(), target_.size()))
        {
            automaticName_ = DeriveAutomaticName(target_.data());
            type_ = DetectItemType(target_.data());
        }
        ImGui::SameLine();
        if (ImGui::Button("浏览文件"))
        {
            if (const auto selected = BrowseForTarget(owner))
                ApplySelectedPath(*selected);
        }
        ImGui::SameLine();
        if (ImGui::Button("浏览文件夹"))
        {
            if (const auto selected = BrowseForFolder(owner))
                ApplySelectedPath(*selected);
        }

        DrawField("启动参数", arguments_.data(), arguments_.size());
        DrawField("工作目录", workingDirectory_.data(), workingDirectory_.size());
        DrawField("自定义图标路径", customIconPath_.data(), customIconPath_.size());
        ImGui::Checkbox("默认以管理员身份运行", &runAsAdministrator_);

        auto drawShortcut = [&](const char* label, LaunchItem::Shortcut& shortcut, int captureId)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(130.0f);
            const std::string text = capturingShortcut_ == captureId ? "请按下快捷键…" : ShortcutText(shortcut);
            ImGui::Button((text + "##" + label).c_str(), ImVec2(250.0f, 0.0f));
            ImGui::SameLine();
            if (ImGui::Button(((capturingShortcut_ == captureId ? "取消##" : "录制##") + std::string(label)).c_str()))
            {
                if (capturingShortcut_ == captureId)
                    capturingShortcut_ = 0;
                else
                {
                    capturingShortcut_ = captureId;
                    captureStartFrame_ = ImGui::GetFrameCount();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(("清除##" + std::string(label)).c_str()))
            {
                shortcut = {};
                if (capturingShortcut_ == captureId) capturingShortcut_ = 0;
            }
        };
        drawShortcut("全局快捷键", globalShortcut_, 1);
        drawShortcut("软件内快捷键", localShortcut_, 2);

        if (capturingShortcut_ != 0 && ImGui::GetFrameCount() > captureStartFrame_ + 1)
        {
            if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
                capturingShortcut_ = 0;
            else
            {
                for (int key = 0x08; key <= 0xFE; ++key)
                {
                    if (IsModifierKey(key) || key == VK_ESCAPE || (key >= VK_LBUTTON && key <= VK_XBUTTON2) ||
                        (GetAsyncKeyState(key) & 0x8000) == 0)
                        continue;
                    LaunchItem::Shortcut& shortcut = capturingShortcut_ == 1 ? globalShortcut_ : localShortcut_;
                    shortcut.enabled = true;
                    shortcut.virtualKey = key;
                    shortcut.modifiers = 0;
                    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) shortcut.modifiers |= HotkeyControl;
                    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) shortcut.modifiers |= HotkeyAlt;
                    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) shortcut.modifiers |= HotkeyShift;
                    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0) shortcut.modifiers |= HotkeyWin;
                    capturingShortcut_ = 0;
                    break;
                }
            }
        }

        LaunchItem preview = BuildItem();
        const CachedIcon previewIcon = iconCache.Get(preview, 64);
        ImGui::Text("自动名称：%s", preview.automaticName.c_str());
        ImGui::Text("项目类型：%s", ItemTypeName(preview.type));
        ImGui::SameLine(430.0f);
        if (previewIcon.texture != nullptr)
        {
            const ImTextureID textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(previewIcon.texture));
            ImGui::Image(ImTextureRef(textureId), ImVec2(64.0f, 64.0f));
        }

        if (!validationError_.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.40f, 1.0f), "%s", validationError_.c_str());

        if (ImGui::Button("保存", ImVec2(90.0f, 0.0f)))
        {
            if (target_[0] == '\0')
            {
                validationError_ = "目标路径不能为空。";
            }
            else if (categoryIndex_ >= config.categories.size())
            {
                validationError_ = "当前分类已不存在。";
            }
            else
            {
                LaunchItem draft = BuildItem();
                MakeLaunchItemPortable(draft);
                if (!editing_)
                    draft.id = MakeId("item");
                auto sameShortcut = [](const LaunchItem::Shortcut& left, const LaunchItem::Shortcut& right)
                {
                    return left.enabled && right.enabled && left.virtualKey == right.virtualKey && left.modifiers == right.modifiers;
                };
                bool globalConflict = draft.globalShortcut.enabled && config.hotkey.enabled &&
                    config.hotkey.trigger == HotkeyTrigger::Keyboard &&
                    draft.globalShortcut.virtualKey == config.hotkey.virtualKey &&
                    draft.globalShortcut.modifiers == config.hotkey.modifiers;
                bool localConflict = false;
                for (const Category& category : config.categories)
                {
                    for (const LaunchItem& existing : category.items)
                    {
                        if (existing.id == draft.id) continue;
                        globalConflict |= sameShortcut(draft.globalShortcut, existing.globalShortcut);
                        localConflict |= sameShortcut(draft.localShortcut, existing.localShortcut);
                    }
                }
                if (globalConflict)
                    validationError_ = "全局快捷键已被 XLaunch 呼出键或其他项目使用。";
                else if (localConflict)
                    validationError_ = "软件内快捷键已被其他项目使用。";
                else if (editing_)
                {
                    if (itemIndex_ < config.categories[categoryIndex_].items.size())
                    {
                        iconCache.Invalidate(config.categories[categoryIndex_].items[itemIndex_].id);
                        config.categories[categoryIndex_].items[itemIndex_] = std::move(draft);
                    }
                    else
                        validationError_ = "原启动项目已不存在。";
                }
                else
                {
                    draft.sortOrder = static_cast<int>(config.categories[categoryIndex_].items.size());
                    config.categories[categoryIndex_].items.push_back(std::move(draft));
                }

                if (validationError_.empty())
                {
                    changed = true;
                    saveImmediately = true;
                    open_ = false;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(90.0f, 0.0f)))
        {
            capturingShortcut_ = 0;
            open_ = false;
        }

        ImGui::End();
    }
}
