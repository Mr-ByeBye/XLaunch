#include "ui/CategoryManager.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "imgui.h"

namespace xlaunch
{
    bool CategoryManager::NameExists(const AppConfig& config, const std::string& name, std::size_t ignoredIndex) const
    {
        for (std::size_t index = 0; index < config.categories.size(); ++index)
        {
            if (index != ignoredIndex && config.categories[index].name == name)
                return true;
        }
        return false;
    }

    void CategoryManager::DeleteCategory(AppConfig& config, std::size_t index, std::size_t& selectedCategory)
    {
        config.categories.erase(config.categories.begin() + static_cast<std::ptrdiff_t>(index));
        if (selectedCategory > index)
            --selectedCategory;
        else if (selectedCategory >= config.categories.size())
            selectedCategory = config.categories.size() - 1;
    }

    std::size_t CategoryManager::HitTestCategory(POINTL screenPoint, std::size_t fallback) const
    {
        const POINT point{ screenPoint.x, screenPoint.y };
        for (std::size_t index = 0; index < categoryRects_.size(); ++index)
        {
            if (PtInRect(&categoryRects_[index], point))
                return index;
        }
        return fallback;
    }

    void CategoryManager::Draw(HWND owner, AppConfig& config, std::size_t& selectedCategory, bool& changed,
        ItemMoveRequest& itemMove, bool externalDrag, std::size_t externalTargetCategory,
        float width, float dpiScale)
    {
        int reorderSource = -1;
        int reorderTarget = -1;
        bool hoveredAnyCategory = false;
        categoryRects_.clear();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f * dpiScale, 3.0f * dpiScale));
        constexpr ImGuiWindowFlags categoryBarFlags =
            ImGuiWindowFlags_HorizontalScrollbar |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::BeginChild("CategoryBar", ImVec2((std::max)(120.0f * dpiScale, width), 35.0f * dpiScale), ImGuiChildFlags_None, categoryBarFlags);
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        {
            const ImGuiIO& io = ImGui::GetIO();
            const float wheel = io.MouseWheelH != 0.0f ? io.MouseWheelH : -io.MouseWheel;
            if (wheel != 0.0f)
                ImGui::SetScrollX(ImGui::GetScrollX() + wheel * 72.0f * dpiScale);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f * dpiScale, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.34f, 0.38f, 0.46f, 0.72f));
        for (std::size_t index = 0; index < config.categories.size(); ++index)
        {
            if (index > 0)
                ImGui::SameLine();
            ImGui::PushID(static_cast<int>(index));

            const bool selected = selectedCategory == index;
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.36f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.42f, 0.76f, 1.0f));
            }
            const float buttonWidth = (std::max)(64.0f * dpiScale, ImGui::CalcTextSize(config.categories[index].name.c_str()).x + 22.0f * dpiScale);
            const bool clicked = ImGui::Button(config.categories[index].name.c_str(), ImVec2(buttonWidth, 29.0f * dpiScale));
            const bool categoryHovered = ImGui::IsItemHovered();
            hoveredAnyCategory |= categoryHovered;
            bool hoveredSwitch = false;
            if (config.appearance.categorySwitchMode == CategorySwitchMode::Hover && categoryHovered &&
                !ImGui::IsAnyMouseDown() && ImGui::GetDragDropPayload() == nullptr)
            {
                if (hoveredCategory_ != static_cast<int>(index))
                {
                    hoveredCategory_ = static_cast<int>(index);
                    hoverStartTime_ = ImGui::GetTime();
                }
                else
                    hoveredSwitch = (ImGui::GetTime() - hoverStartTime_) * 1000.0 >= config.appearance.categoryHoverDelayMs;
            }
            if (clicked || hoveredSwitch)
                selectedCategory = index;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                renameIndex_ = static_cast<int>(index);
                strncpy_s(nameBuffer_.data(), nameBuffer_.size(), config.categories[index].name.c_str(), _TRUNCATE);
                validationError_.clear();
                focusRenameInput_ = true;
            }
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            POINT topLeft{ static_cast<LONG>(itemMin.x), static_cast<LONG>(itemMin.y) };
            POINT bottomRight{ static_cast<LONG>(itemMax.x), static_cast<LONG>(itemMax.y) };
            ClientToScreen(owner, &topLeft);
            ClientToScreen(owner, &bottomRight);
            categoryRects_.push_back(RECT{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y });
            if (externalDrag && externalTargetCategory == index)
                ImGui::GetForegroundDrawList()->AddRect(itemMin, itemMax, IM_COL32(85, 150, 255, 255), 3.0f, 0, 2.0f);
            if (selected)
                ImGui::PopStyleColor(2);

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                const int source = static_cast<int>(index);
                ImGui::SetDragDropPayload("XLAUNCH_CATEGORY", &source, sizeof(source));
                ImGui::Text("移动分类：%s", config.categories[index].name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("XLAUNCH_CATEGORY"))
                {
                    reorderSource = *static_cast<const int*>(payload->Data);
                    reorderTarget = static_cast<int>(index);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("XLAUNCH_ITEM"))
                {
                    itemMove.requested = true;
                    itemMove.source = *static_cast<const ItemDragPayload*>(payload->Data);
                    itemMove.destinationCategory = index;
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem("CategoryMenu"))
            {
                if (ImGui::MenuItem("重命名"))
                {
                    renameIndex_ = static_cast<int>(index);
                    strncpy_s(nameBuffer_.data(), nameBuffer_.size(), config.categories[index].name.c_str(), _TRUNCATE);
                    validationError_.clear();
                    focusRenameInput_ = true;
                }
                if (ImGui::MenuItem("删除", nullptr, false, config.categories.size() > 1))
                {
                    deleteIndex_ = static_cast<int>(index);
                    moveDestination_ = index == 0 ? 1 : 0;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        if (config.appearance.categorySwitchMode != CategorySwitchMode::Hover || !hoveredAnyCategory ||
            ImGui::IsAnyMouseDown() || ImGui::GetDragDropPayload() != nullptr)
        {
            hoveredCategory_ = -1;
            hoverStartTime_ = 0.0;
        }

        ImGui::SameLine();
        if (ImGui::Button("+##AddCategory", ImVec2(32.0f * dpiScale, 29.0f * dpiScale)))
            addRequested_ = true;

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        if (reorderSource >= 0 && reorderTarget >= 0 && reorderSource != reorderTarget &&
            reorderSource < static_cast<int>(config.categories.size()) && reorderTarget < static_cast<int>(config.categories.size()))
        {
            const std::string selectedId = config.categories[selectedCategory].id;
            Category moved = std::move(config.categories[reorderSource]);
            config.categories.erase(config.categories.begin() + reorderSource);
            config.categories.insert(config.categories.begin() + reorderTarget, std::move(moved));
            const auto selected = std::find_if(config.categories.begin(), config.categories.end(),
                [&](const Category& category) { return category.id == selectedId; });
            selectedCategory = static_cast<std::size_t>(std::distance(config.categories.begin(), selected));
            changed = true;
        }

        if (addRequested_)
        {
            nameBuffer_.fill('\0');
            validationError_.clear();
            ImGui::OpenPopup("新增分类");
            addRequested_ = false;
        }
        if (ImGui::BeginPopupModal("新增分类", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("分类名称");
            ImGui::SetNextItemWidth(280.0f);
            const bool submitted = ImGui::InputText("##NewCategoryName", nameBuffer_.data(), nameBuffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
            if (!validationError_.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.40f, 1.0f), "%s", validationError_.c_str());
            if (submitted || ImGui::Button("新增", ImVec2(90.0f, 0.0f)))
            {
                const std::string name = nameBuffer_.data();
                if (name.empty())
                    validationError_ = "分类名称不能为空。";
                else if (NameExists(config, name, (std::numeric_limits<std::size_t>::max)()))
                    validationError_ = "分类名称不能重复。";
                else
                {
                    config.categories.push_back(Category{ MakeId("category"), name, {} });
                    selectedCategory = config.categories.size() - 1;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(90.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (renameIndex_ >= 0)
            ImGui::OpenPopup("重命名分类");
        if (ImGui::BeginPopupModal("重命名分类", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("分类名称");
            ImGui::SetNextItemWidth(280.0f);
            if (focusRenameInput_)
            {
                ImGui::SetKeyboardFocusHere();
                focusRenameInput_ = false;
            }
            const bool submitted = ImGui::InputText("##RenameCategoryName", nameBuffer_.data(), nameBuffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
            if (!validationError_.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.40f, 1.0f), "%s", validationError_.c_str());
            if (submitted || ImGui::Button("保存", ImVec2(90.0f, 0.0f)))
            {
                const std::string name = nameBuffer_.data();
                if (name.empty())
                    validationError_ = "分类名称不能为空。";
                else if (NameExists(config, name, static_cast<std::size_t>(renameIndex_)))
                    validationError_ = "分类名称不能重复。";
                else
                {
                    config.categories[renameIndex_].name = name;
                    renameIndex_ = -1;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(90.0f, 0.0f)))
            {
                renameIndex_ = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (deleteIndex_ >= 0)
        {
            const bool empty = config.categories[deleteIndex_].items.empty();
            ImGui::OpenPopup(empty ? "确认删除分类" : "删除非空分类");
        }
        if (ImGui::BeginPopupModal("确认删除分类", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("确认删除分类“%s”？", config.categories[deleteIndex_].name.c_str());
            if (ImGui::Button("删除", ImVec2(90.0f, 0.0f)))
            {
                DeleteCategory(config, deleteIndex_, selectedCategory);
                deleteIndex_ = -1;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(90.0f, 0.0f)))
            {
                deleteIndex_ = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("删除非空分类", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("分类“%s”包含 %zu 个项目。", config.categories[deleteIndex_].name.c_str(), config.categories[deleteIndex_].items.size());
            if (ImGui::BeginCombo("移动到", config.categories[moveDestination_].name.c_str()))
            {
                for (std::size_t index = 0; index < config.categories.size(); ++index)
                {
                    if (index == static_cast<std::size_t>(deleteIndex_))
                        continue;
                    if (ImGui::Selectable(config.categories[index].name.c_str(), moveDestination_ == static_cast<int>(index)))
                        moveDestination_ = static_cast<int>(index);
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("移动项目并删除分类"))
            {
                auto& source = config.categories[deleteIndex_].items;
                auto& destination = config.categories[moveDestination_].items;
                destination.insert(destination.end(), std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
                for (std::size_t index = 0; index < destination.size(); ++index)
                    destination[index].sortOrder = static_cast<int>(index);
                DeleteCategory(config, deleteIndex_, selectedCategory);
                deleteIndex_ = -1;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("同时删除项目"))
            {
                DeleteCategory(config, deleteIndex_, selectedCategory);
                deleteIndex_ = -1;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消"))
            {
                deleteIndex_ = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}
