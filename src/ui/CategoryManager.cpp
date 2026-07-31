#include "ui/CategoryManager.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "imgui.h"

namespace xlaunch
{
    namespace
    {
        void DrawRotatedText(ImDrawList* drawList, const ImVec2& cellMin, const ImVec2& cellMax,
            const char* text, bool clockwise, ImU32 color)
        {
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const int vertexStart = drawList->VtxBuffer.Size;
            drawList->AddText(cellMin, color, text);
            const ImVec2 origin{
                cellMin.x + ((cellMax.x - cellMin.x) - textSize.y) * 0.5f,
                cellMin.y + ((cellMax.y - cellMin.y) - textSize.x) * 0.5f
            };
            for (int vertex = vertexStart; vertex < drawList->VtxBuffer.Size; ++vertex)
            {
                const ImVec2 relative{
                    drawList->VtxBuffer[vertex].pos.x - cellMin.x,
                    drawList->VtxBuffer[vertex].pos.y - cellMin.y
                };
                drawList->VtxBuffer[vertex].pos = clockwise
                    ? ImVec2(origin.x + textSize.y - relative.y, origin.y + relative.x)
                    : ImVec2(origin.x + relative.y, origin.y + textSize.x - relative.x);
            }
        }

        bool DrawPinIconButton(bool pinned, float size)
        {
            const bool clicked = ImGui::InvisibleButton("##PinCategoryBar", ImVec2(size, size));
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 fill = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered :
                pinned ? ImGuiCol_ButtonActive : ImGuiCol_Button);
            drawList->AddRectFilled(minimum, maximum, fill, 3.0f);
            drawList->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border), 3.0f);
            const ImVec2 center{ (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f };
            const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);
            const float scale = size / 32.0f;
            drawList->AddRectFilled(
                ImVec2(center.x - 6.0f * scale, center.y - 7.0f * scale),
                ImVec2(center.x + 6.0f * scale, center.y - 1.0f * scale),
                iconColor, 1.5f * scale);
            drawList->AddTriangleFilled(
                ImVec2(center.x - 4.0f * scale, center.y - 1.0f * scale),
                ImVec2(center.x + 4.0f * scale, center.y - 1.0f * scale),
                ImVec2(center.x, center.y + 4.0f * scale), iconColor);
            drawList->AddLine(
                ImVec2(center.x, center.y + 3.0f * scale),
                ImVec2(center.x, center.y + 9.0f * scale), iconColor, 1.5f * scale);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(pinned ? "取消钉住" : "钉住");
            return clicked;
        }
    }

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
        float width, float height, float dpiScale, bool& keepVisible, bool& saveImmediately)
    {
        int reorderSource = -1;
        int reorderTarget = -1;
        bool hoveredAnyCategory = false;
        categoryRects_.clear();
        const CategoryBarLayout layout = config.appearance.categoryBarLayout;
        const bool sideLayout = layout == CategoryBarLayout::Left || layout == CategoryBarLayout::Right;
        const bool wrapLayout = layout == CategoryBarLayout::TopWrap;
        const bool verticalText = sideLayout &&
            config.appearance.categoryBarTextDirection == CategoryBarTextDirection::Vertical;
        const float spacing = 3.0f * dpiScale;
        const float sideInset = 4.0f * dpiScale;
        const float standardHeight = 27.0f * dpiScale;
        const float toolButtonSize = 29.0f * dpiScale;
        const float toolbarWidth = toolButtonSize * 2.0f + spacing;
        const bool inlineSideToolbar = width >= toolbarWidth + sideInset * 2.0f;
        const float toolbarHeight = sideLayout
            ? (inlineSideToolbar ? toolButtonSize : toolButtonSize * 2.0f + spacing)
            : toolButtonSize;
        const float sideToolbarBottomInset = sideLayout ? 4.0f * dpiScale : 0.0f;
        const float categoryAreaWidth = sideLayout ? width :
            (std::max)(1.0f, width - toolbarWidth - spacing);
        const float categoryAreaHeight = sideLayout
            ? (std::max)(1.0f, height - toolbarHeight - spacing - sideToolbarBottomInset)
            : 0.0f;

        float barHeight = 35.0f * dpiScale;
        if (wrapLayout)
        {
            int rows = 1;
            float used = 0.0f;
            for (const Category& category : config.categories)
            {
                const float buttonWidth = (std::max)(64.0f * dpiScale,
                    ImGui::CalcTextSize(category.name.c_str()).x + 22.0f * dpiScale);
                if (used > 0.0f && used + spacing + buttonWidth > categoryAreaWidth - 4.0f * dpiScale)
                {
                    ++rows;
                    used = 0.0f;
                }
                used += (used > 0.0f ? spacing : 0.0f) + buttonWidth;
            }
            barHeight = rows * standardHeight + (rows - 1) * spacing + 8.0f * dpiScale;
        }

        const ImVec2 completeBarMinimum = ImGui::GetCursorScreenPos();
        const float completeBarHeight = sideLayout ? height : barHeight;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("CategoryBar", ImVec2(width, completeBarHeight),
            ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        float categoryContentExtent = 0.0f;
        if (sideLayout)
        {
            for (const Category& category : config.categories)
            {
                const float buttonHeight = verticalText
                    ? (std::max)(64.0f * dpiScale,
                        ImGui::CalcTextSize(category.name.c_str()).x + 22.0f * dpiScale)
                    : toolButtonSize;
                categoryContentExtent += buttonHeight + spacing;
            }
            if (!config.categories.empty())
                categoryContentExtent -= spacing;
        }
        else if (!wrapLayout)
        {
            for (const Category& category : config.categories)
            {
                categoryContentExtent += (std::max)(64.0f * dpiScale,
                    ImGui::CalcTextSize(category.name.c_str()).x + 22.0f * dpiScale) + spacing;
            }
            if (!config.categories.empty())
                categoryContentExtent -= spacing;
        }

        const ImVec2 categoryAreaMinimum = ImGui::GetWindowPos();
        const ImVec2 categoryAreaMaximum{
            categoryAreaMinimum.x + categoryAreaWidth,
            categoryAreaMinimum.y + (sideLayout ? categoryAreaHeight : barHeight)
        };
        const ImVec2 mousePosition = ImGui::GetIO().MousePos;
        const bool categoryAreaHovered =
            mousePosition.x >= categoryAreaMinimum.x && mousePosition.x < categoryAreaMaximum.x &&
            mousePosition.y >= categoryAreaMinimum.y && mousePosition.y < categoryAreaMaximum.y &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (sideLayout)
        {
            horizontalScroll_ = 0.0f;
            const float maximumScroll = (std::max)(0.0f,
                categoryContentExtent + 6.0f * dpiScale - categoryAreaHeight);
            verticalScroll_ = std::clamp(verticalScroll_, 0.0f, maximumScroll);
            if (categoryAreaHovered && ImGui::GetIO().MouseWheel != 0.0f)
            {
                verticalScroll_ = std::clamp(
                    verticalScroll_ - ImGui::GetIO().MouseWheel * 72.0f * dpiScale,
                    0.0f, maximumScroll);
            }
        }
        else
        {
            verticalScroll_ = 0.0f;
            if (wrapLayout)
                horizontalScroll_ = 0.0f;
            else
            {
                const float maximumScroll = (std::max)(0.0f,
                    categoryContentExtent + 4.0f * dpiScale - categoryAreaWidth);
                horizontalScroll_ = std::clamp(horizontalScroll_, 0.0f, maximumScroll);
                if (categoryAreaHovered)
                {
                    const ImGuiIO& io = ImGui::GetIO();
                    const float wheel = io.MouseWheelH != 0.0f ? io.MouseWheelH : -io.MouseWheel;
                    if (wheel != 0.0f)
                    {
                        horizontalScroll_ = std::clamp(
                            horizontalScroll_ + wheel * 72.0f * dpiScale,
                            0.0f, maximumScroll);
                    }
                }
            }
        }

        ImGui::PushClipRect(categoryAreaMinimum, categoryAreaMaximum, true);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.34f, 0.38f, 0.46f, 0.72f));
        float nextButtonX = 2.0f * dpiScale - horizontalScroll_;
        float nextButtonY = sideLayout
            ? 3.0f * dpiScale - verticalScroll_
            : wrapLayout
                ? 4.0f * dpiScale
                : (barHeight - toolButtonSize) * 0.5f + (toolButtonSize - standardHeight);
        for (std::size_t index = 0; index < config.categories.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));

            const bool selected = selectedCategory == index;
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.36f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.42f, 0.76f, 1.0f));
            }
            const float naturalWidth = (std::max)(64.0f * dpiScale,
                ImGui::CalcTextSize(config.categories[index].name.c_str()).x + 22.0f * dpiScale);
            float buttonWidth = sideLayout
                ? (verticalText
                    ? (std::min)(toolButtonSize, (std::max)(1.0f, width - sideInset))
                    : (std::max)(1.0f, width - sideInset * 2.0f))
                : naturalWidth;
            float buttonHeight = sideLayout ? toolButtonSize : standardHeight;
            if (verticalText)
            {
                buttonHeight = (std::max)(64.0f * dpiScale,
                    ImGui::CalcTextSize(config.categories[index].name.c_str()).x + 22.0f * dpiScale);
            }

            if (sideLayout)
            {
                const float buttonX = (std::max)(0.0f, (width - buttonWidth) * 0.5f);
                ImGui::SetCursorPos(ImVec2(std::floor(buttonX), std::floor(nextButtonY)));
            }
            else
            {
                if (wrapLayout && nextButtonX > 2.0f * dpiScale &&
                    nextButtonX + buttonWidth > categoryAreaWidth - 2.0f * dpiScale)
                {
                    nextButtonX = 2.0f * dpiScale;
                    nextButtonY += standardHeight + spacing;
                }
                ImGui::SetCursorPos(ImVec2(std::floor(nextButtonX), std::floor(nextButtonY)));
            }

            bool clicked = false;
            if (verticalText)
            {
                clicked = ImGui::InvisibleButton("##VerticalCategory", ImVec2(buttonWidth, buttonHeight));
                const ImVec2 itemMin = ImGui::GetItemRectMin();
                const ImVec2 itemMax = ImGui::GetItemRectMax();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const bool hovered = ImGui::IsItemHovered();
                const ImU32 fill = ImGui::GetColorU32(selected ? ImGuiCol_Button :
                    hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
                drawList->AddRectFilled(itemMin, itemMax, fill, 3.0f);
                drawList->AddRect(itemMin, itemMax, ImGui::GetColorU32(ImGuiCol_Border), 3.0f);
                DrawRotatedText(drawList, itemMin, itemMax, config.categories[index].name.c_str(),
                    config.appearance.categoryBarVerticalReading == CategoryBarVerticalReading::TopToBottom,
                    ImGui::GetColorU32(ImGuiCol_Text));
            }
            else
                clicked = ImGui::Button(config.categories[index].name.c_str(), ImVec2(buttonWidth, buttonHeight));
            if (sideLayout)
                nextButtonY += buttonHeight + spacing;
            else
                nextButtonX += buttonWidth + spacing;
            const bool categoryHovered = ImGui::IsItemHovered();
            hoveredAnyCategory |= categoryHovered;
            if (categoryHovered && sideLayout)
                ImGui::SetTooltip("%s", config.categories[index].name.c_str());
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
            const ImVec2 clippedItemMin{
                (std::max)(itemMin.x, categoryAreaMinimum.x),
                (std::max)(itemMin.y, categoryAreaMinimum.y)
            };
            const ImVec2 clippedItemMax{
                (std::min)(itemMax.x, categoryAreaMaximum.x),
                (std::min)(itemMax.y, categoryAreaMaximum.y)
            };
            POINT topLeft{ static_cast<LONG>(clippedItemMin.x), static_cast<LONG>(clippedItemMin.y) };
            POINT bottomRight{ static_cast<LONG>(clippedItemMax.x), static_cast<LONG>(clippedItemMax.y) };
            ClientToScreen(owner, &topLeft);
            ClientToScreen(owner, &bottomRight);
            categoryRects_.push_back(clippedItemMin.x < clippedItemMax.x && clippedItemMin.y < clippedItemMax.y
                ? RECT{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y }
                : RECT{});
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

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::PopClipRect();

        auto drawToolbar = [&]
        {
            if (ImGui::Button("+##AddCategory", ImVec2(toolButtonSize, toolButtonSize)))
                addRequested_ = true;
            if (!sideLayout || inlineSideToolbar)
                ImGui::SameLine(0.0f, spacing);
            else
                ImGui::SetCursorPosX(std::floor((width - toolButtonSize) * 0.5f));
            if (DrawPinIconButton(keepVisible, toolButtonSize))
            {
                keepVisible = !keepVisible;
                SetWindowPos(owner, keepVisible ? HWND_TOPMOST : HWND_NOTOPMOST,
                    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                changed = true;
                saveImmediately = true;
            }
        };
        if (sideLayout)
        {
            const float toolbarActualWidth = inlineSideToolbar ? toolbarWidth : toolButtonSize;
            const float toolbarX = (std::max)(0.0f, (width - toolbarActualWidth) * 0.5f);
            ImGui::SetCursorPos(ImVec2(
                std::floor(toolbarX),
                std::floor((std::max)(0.0f, height - toolbarHeight - sideToolbarBottomInset))));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
            drawToolbar();
            ImGui::PopStyleVar();
        }
        else
        {
            ImGui::SetCursorPos(ImVec2(
                (std::max)(0.0f, width - toolbarWidth),
                (std::max)(0.0f, (barHeight - toolButtonSize) * 0.5f)));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
            drawToolbar();
            ImGui::PopStyleVar();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        if (sideLayout)
        {
            const float x = layout == CategoryBarLayout::Left
                ? completeBarMinimum.x + width - 0.5f : completeBarMinimum.x + 0.5f;
            ImGui::GetForegroundDrawList()->AddLine(
                ImVec2(x, completeBarMinimum.y),
                ImVec2(x, completeBarMinimum.y + height),
                ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
        }

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
