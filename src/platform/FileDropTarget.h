#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <windows.h>
#include <oleidl.h>

namespace xlaunch
{
    struct DroppedShellItem
    {
        std::wstring fileSystemPath;
        std::wstring parsingName;
        std::wstring displayName;
    };

    class FileDropTarget final : public IDropTarget
    {
    public:
        using DragStateCallback = std::function<void(bool, POINTL)>;
        using DropCallback = std::function<void(std::vector<DroppedShellItem>, POINTL)>;

        FileDropTarget(DragStateCallback dragStateCallback, DropCallback dropCallback);

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* dataObject, DWORD keyState, POINTL point, DWORD* effect) override;
        HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState, POINTL point, DWORD* effect) override;
        HRESULT STDMETHODCALLTYPE DragLeave() override;
        HRESULT STDMETHODCALLTYPE Drop(IDataObject* dataObject, DWORD keyState, POINTL point, DWORD* effect) override;

    private:
        [[nodiscard]] bool SupportsItems(IDataObject* dataObject) const;
        [[nodiscard]] std::vector<DroppedShellItem> ReadItems(IDataObject* dataObject) const;

        std::atomic<ULONG> referenceCount_{ 1 };
        DragStateCallback dragStateCallback_;
        DropCallback dropCallback_;
        bool acceptsCurrentDrag_ = false;
    };
}
