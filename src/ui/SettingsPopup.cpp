#include "ui/SettingsPopup.h"

#include <string>
#include <cstring>

#include "imgui.h"

namespace xlaunch
{
    namespace
    {
        std::string WideToUtf8(const std::wstring& value)
        {
            if (value.empty()) return {};
            const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string result(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
            return result;
        }

        std::string KeyName(int virtualKey)
        {
            if (virtualKey >= 'A' && virtualKey <= 'Z') return std::string(1, static_cast<char>(virtualKey));
            if (virtualKey >= '0' && virtualKey <= '9') return std::string(1, static_cast<char>(virtualKey));
            if (virtualKey >= VK_F1 && virtualKey <= VK_F24) return "F" + std::to_string(virtualKey - VK_F1 + 1);
            if (virtualKey == VK_SPACE) return "Space";
            if (virtualKey == VK_TAB) return "Tab";
            if (virtualKey == VK_RETURN) return "Enter";
            if (virtualKey == VK_BACK) return "Backspace";
            if (virtualKey == VK_DELETE) return "Delete";
            if (virtualKey == VK_INSERT) return "Insert";
            if (virtualKey == VK_HOME) return "Home";
            if (virtualKey == VK_END) return "End";
            if (virtualKey == VK_PRIOR) return "Page Up";
            if (virtualKey == VK_NEXT) return "Page Down";

            const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
            wchar_t name[64]{};
            if (GetKeyNameTextW(static_cast<LONG>(scanCode << 16), name, static_cast<int>(std::size(name))) > 0)
                return WideToUtf8(name);
            return "VK " + std::to_string(virtualKey);
        }

        std::string HotkeyText(const HotkeySettings& hotkey)
        {
            std::string result;
            auto append = [&](const char* value) { if (!result.empty()) result += " + "; result += value; };
            if ((hotkey.modifiers & HotkeyControl) != 0) append("Ctrl");
            if ((hotkey.modifiers & HotkeyAlt) != 0) append("Alt");
            if ((hotkey.modifiers & HotkeyShift) != 0) append("Shift");
            if ((hotkey.modifiers & HotkeyWin) != 0) append("Win");
            if (!result.empty()) result += " + ";
            result += KeyName(hotkey.virtualKey);
            return result;
        }

        bool IsModifierKey(int key)
        {
            return key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
                key == VK_MENU || key == VK_LMENU || key == VK_RMENU ||
                key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
                key == VK_LWIN || key == VK_RWIN;
        }

        void SectionTitle(const char* title)
        {
            ImGui::TextUnformatted(title);
            ImGui::Separator();
        }
    }

    SettingsActions SettingsPopup::Draw(HWND owner, AppConfig& config, bool& changed)
    {
        SettingsActions actions;
        if (openRequested_)
        {
            strncpy_s(titleBuffer_.data(), titleBuffer_.size(), config.window.title.c_str(), _TRUNCATE);
            ImGui::OpenPopup("设置");
            openRequested_ = false;
        }

        ImGui::SetNextWindowSize(ImVec2(650.0f, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("设置", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
        {
            if (capturingHotkey_)
            {
                capturingHotkey_ = false;
                actions.hotkeyChanged = true;
            }
            return actions;
        }

        if (ImGui::BeginTable("SettingsColumns", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableNextColumn();
            SectionTitle("外观");
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("标题", titleBuffer_.data(), titleBuffer_.size()))
            {
                config.window.title = titleBuffer_.data();
                changed = true;
                actions.windowTitleChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("居中", &config.window.centerTitle)) changed = true;
            changed |= ImGui::Checkbox("显示名称", &config.appearance.showNames);
            ImGui::SameLine(135.0f);
            changed |= ImGui::Checkbox("显示边框", &config.appearance.showBorders);

            constexpr int sizes[]{ 32, 40, 48, 56, 64 };
            ImGui::SetNextItemWidth(82.0f);
            if (ImGui::BeginCombo("图标", std::to_string(config.appearance.iconSize).c_str()))
            {
                for (const int size : sizes)
                {
                    if (ImGui::Selectable(std::to_string(size).c_str(), config.appearance.iconSize == size))
                    {
                        config.appearance.iconSize = size;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(72.0f);
            changed |= ImGui::DragFloat("横距", &config.appearance.horizontalSpacing, 1.0f, 4.0f, 40.0f, "%.0f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(72.0f);
            changed |= ImGui::DragFloat("纵距", &config.appearance.verticalSpacing, 1.0f, 4.0f, 40.0f, "%.0f");
            ImGui::SetNextItemWidth(180.0f);
            int opacityPercent = static_cast<int>(config.appearance.windowOpacity * 100.0f + 0.5f);
            if (ImGui::SliderInt("整体透明度", &opacityPercent, 35, 100, "%d%%"))
            {
                config.appearance.windowOpacity = static_cast<float>(opacityPercent) / 100.0f;
                changed = true;
                actions.windowOpacityChanged = true;
            }
            changed |= ImGui::Checkbox("调整窗口后自动贴合网格", &config.appearance.fitWindowToGridAfterResize);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("横向贴合完整列，纵向贴合最近的完整可见行；剩余项目可滚动查看。");

            ImGui::Spacing();
            SectionTitle("启动位置");
            constexpr const char* positionNames[]{ "屏幕正中", "屏幕角落", "用户自定义", "鼠标位置" };
            int position = static_cast<int>(config.window.startupPosition);
            ImGui::SetNextItemWidth(125.0f);
            if (ImGui::Combo("##PositionMode", &position, positionNames, 4))
            {
                config.window.startupPosition = static_cast<StartupPositionMode>(position);
                changed = true;
            }
            ImGui::SameLine();
            if (config.window.startupPosition == StartupPositionMode::Corner)
            {
                constexpr const char* corners[]{ "左上", "右上", "左下", "右下" };
                int corner = static_cast<int>(config.window.corner);
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::Combo("##Corner", &corner, corners, 4))
                {
                    config.window.corner = static_cast<ScreenCorner>(corner);
                    changed = true;
                }
            }
            else if (config.window.startupPosition == StartupPositionMode::Custom)
            {
                if (ImGui::Button("使用当前位置"))
                {
                    RECT bounds{};
                    if (GetWindowRect(owner, &bounds))
                    {
                        config.window.customX = bounds.left;
                        config.window.customY = bounds.top;
                        changed = true;
                    }
                }
                ImGui::TextDisabled("%d, %d", config.window.customX, config.window.customY);
            }
            else if (config.window.startupPosition == StartupPositionMode::Cursor)
                ImGui::TextDisabled("每次呼出时定位到鼠标附近");
            else
                ImGui::TextDisabled("显示在主屏幕工作区域正中");

            ImGui::TableNextColumn();
            SectionTitle("快捷呼出");
            if (ImGui::Checkbox("启用", &config.hotkey.enabled))
            {
                changed = true;
                actions.hotkeyChanged = true;
            }
            ImGui::SameLine();
            constexpr const char* triggerNames[]{ "鼠标手势", "键盘快捷键" };
            int trigger = static_cast<int>(config.hotkey.trigger);
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("##Trigger", &trigger, triggerNames, 2))
            {
                config.hotkey.trigger = static_cast<HotkeyTrigger>(trigger);
                changed = true;
                actions.hotkeyChanged = true;
            }

            if (config.hotkey.trigger == HotkeyTrigger::Keyboard)
            {
                const std::string label = capturingHotkey_ ? "请按下快捷键…" : HotkeyText(config.hotkey);
                ImGui::Button(label.c_str(), ImVec2(180.0f, 0.0f));
                ImGui::SameLine();
                if (ImGui::Button(capturingHotkey_ ? "取消" : "录制"))
                {
                    if (capturingHotkey_)
                    {
                        capturingHotkey_ = false;
                        actions.hotkeyChanged = true;
                    }
                    else
                    {
                        capturingHotkey_ = true;
                        captureStartFrame_ = ImGui::GetFrameCount();
                        actions.suspendHotkey = true;
                    }
                }
            }
            else
            {
                constexpr const char* mouseNames[]{ "无", "中键", "侧键 1", "侧键 2" };
                int primary = static_cast<int>(config.hotkey.mouseButton);
                ImGui::SetNextItemWidth(92.0f);
                if (ImGui::Combo("主键", &primary, mouseNames, 4))
                {
                    config.hotkey.mouseButton = static_cast<MouseButton>(primary);
                    changed = true; actions.hotkeyChanged = true;
                }
                ImGui::SameLine();
                int held = static_cast<int>(config.hotkey.heldMouseButton);
                ImGui::SetNextItemWidth(92.0f);
                if (ImGui::Combo("按住", &held, mouseNames, 4))
                {
                    config.hotkey.heldMouseButton = static_cast<MouseButton>(held);
                    changed = true; actions.hotkeyChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("双击", &config.hotkey.mouseDoubleClick))
                {
                    changed = true; actions.hotkeyChanged = true;
                }
            }

            ImGui::Spacing();
            SectionTitle("系统与备份");
            if (ImGui::Checkbox("开机自启", &config.startWithWindows))
            {
                changed = true;
                actions.startupChanged = true;
            }
            ImGui::SameLine(130.0f);
            if (ImGui::Checkbox("自动备份", &config.backup.automatic)) changed = true;
            ImGui::SameLine();
            ImGui::BeginDisabled(!config.backup.automatic);
            ImGui::SetNextItemWidth(55.0f);
            if (ImGui::DragInt("份", &config.backup.keepCount, 1.0f, 1, 50)) changed = true;
            ImGui::EndDisabled();

            if (ImGui::Button("立即备份")) actions.backupNow = true;
            ImGui::SameLine();
            if (ImGui::Button("导出")) actions.exportConfig = true;
            ImGui::SameLine();
            if (ImGui::Button("导入")) actions.importConfig = true;
            ImGui::SameLine();
            if (ImGui::Button("配置目录")) actions.openConfigDirectory = true;
            ImGui::EndTable();
        }

        if (capturingHotkey_ && ImGui::GetFrameCount() > captureStartFrame_ + 1)
        {
            if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
            {
                capturingHotkey_ = false;
                actions.hotkeyChanged = true;
            }
            else
            {
                for (int key = 0x08; key <= 0xFE; ++key)
                {
                    if (IsModifierKey(key) || key == VK_ESCAPE || (key >= VK_LBUTTON && key <= VK_XBUTTON2)) continue;
                    if ((GetAsyncKeyState(key) & 0x8000) == 0) continue;
                    int modifiers = 0;
                    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= HotkeyControl;
                    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) modifiers |= HotkeyAlt;
                    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= HotkeyShift;
                    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0) modifiers |= HotkeyWin;
                    config.hotkey.modifiers = modifiers;
                    config.hotkey.virtualKey = key;
                    capturingHotkey_ = false;
                    changed = true;
                    actions.hotkeyChanged = true;
                    break;
                }
            }
        }

        const float closeWidth = 82.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - closeWidth - ImGui::GetStyle().WindowPadding.x);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.38f, 0.48f, 0.66f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.22f, 0.34f, 1.0f));
        if (ImGui::Button("关闭", ImVec2(closeWidth, 0.0f)))
        {
            if (capturingHotkey_)
            {
                capturingHotkey_ = false;
                actions.hotkeyChanged = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        ImGui::EndPopup();
        return actions;
    }
}
