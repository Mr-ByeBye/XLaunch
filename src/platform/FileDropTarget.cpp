#include "platform/FileDropTarget.h"

#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>

namespace xlaunch
{
    namespace
    {
        FORMATETC FileDropFormat()
        {
            return FORMATETC{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        }
    }

    FileDropTarget::FileDropTarget(DragStateCallback dragStateCallback, DropCallback dropCallback)
        : dragStateCallback_(std::move(dragStateCallback)), dropCallback_(std::move(dropCallback))
    {
    }

    HRESULT FileDropTarget::QueryInterface(REFIID interfaceId, void** object)
    {
        if (object == nullptr)
            return E_POINTER;
        if (interfaceId == IID_IUnknown || interfaceId == IID_IDropTarget)
        {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG FileDropTarget::AddRef()
    {
        return ++referenceCount_;
    }

    ULONG FileDropTarget::Release()
    {
        const ULONG remaining = --referenceCount_;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    bool FileDropTarget::SupportsItems(IDataObject* dataObject) const
    {
        if (dataObject == nullptr)
            return false;
        FORMATETC format = FileDropFormat();
        if (SUCCEEDED(dataObject->QueryGetData(&format)))
            return true;
        Microsoft::WRL::ComPtr<IShellItemArray> items;
        return SUCCEEDED(SHCreateShellItemArrayFromDataObject(dataObject, IID_PPV_ARGS(&items)));
    }

    std::vector<DroppedShellItem> FileDropTarget::ReadItems(IDataObject* dataObject) const
    {
        std::vector<DroppedShellItem> items;
        Microsoft::WRL::ComPtr<IShellItemArray> shellItems;
        if (dataObject != nullptr && SUCCEEDED(SHCreateShellItemArrayFromDataObject(dataObject, IID_PPV_ARGS(&shellItems))))
        {
            DWORD count = 0;
            shellItems->GetCount(&count);
            items.reserve(count);
            for (DWORD index = 0; index < count; ++index)
            {
                Microsoft::WRL::ComPtr<IShellItem> shellItem;
                if (FAILED(shellItems->GetItemAt(index, &shellItem)))
                    continue;
                DroppedShellItem item;
                auto readName = [&](SIGDN kind, std::wstring& destination)
                {
                    PWSTR value = nullptr;
                    if (SUCCEEDED(shellItem->GetDisplayName(kind, &value)) && value != nullptr)
                    {
                        destination = value;
                        CoTaskMemFree(value);
                    }
                };
                readName(SIGDN_FILESYSPATH, item.fileSystemPath);
                readName(SIGDN_DESKTOPABSOLUTEPARSING, item.parsingName);
                readName(SIGDN_NORMALDISPLAY, item.displayName);
                if (!item.fileSystemPath.empty() || !item.parsingName.empty())
                    items.push_back(std::move(item));
            }
            if (!items.empty())
                return items;
        }

        FORMATETC format = FileDropFormat();
        STGMEDIUM medium{};
        if (dataObject == nullptr || FAILED(dataObject->GetData(&format, &medium)))
            return items;

        const HDROP drop = static_cast<HDROP>(medium.hGlobal);
        if (drop != nullptr)
        {
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            items.reserve(count);
            for (UINT index = 0; index < count; ++index)
            {
                const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                std::wstring path(length + 1, L'\0');
                DragQueryFileW(drop, index, path.data(), static_cast<UINT>(path.size()));
                path.resize(length);
                items.push_back(DroppedShellItem{ path, path, {} });
            }
        }
        ReleaseStgMedium(&medium);
        return items;
    }

    HRESULT FileDropTarget::DragEnter(IDataObject* dataObject, DWORD, POINTL point, DWORD* effect)
    {
        acceptsCurrentDrag_ = SupportsItems(dataObject);
        if (effect != nullptr)
            *effect = acceptsCurrentDrag_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (dragStateCallback_)
            dragStateCallback_(acceptsCurrentDrag_, point);
        return S_OK;
    }

    HRESULT FileDropTarget::DragOver(DWORD, POINTL point, DWORD* effect)
    {
        if (effect != nullptr)
            *effect = acceptsCurrentDrag_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (dragStateCallback_)
            dragStateCallback_(acceptsCurrentDrag_, point);
        return S_OK;
    }

    HRESULT FileDropTarget::DragLeave()
    {
        acceptsCurrentDrag_ = false;
        if (dragStateCallback_)
            dragStateCallback_(false, POINTL{});
        return S_OK;
    }

    HRESULT FileDropTarget::Drop(IDataObject* dataObject, DWORD, POINTL point, DWORD* effect)
    {
        std::vector<DroppedShellItem> items = ReadItems(dataObject);
        const bool accepted = acceptsCurrentDrag_ && !items.empty();
        acceptsCurrentDrag_ = false;
        if (dragStateCallback_)
            dragStateCallback_(false, point);
        if (accepted && dropCallback_)
            dropCallback_(std::move(items), point);
        if (effect != nullptr)
            *effect = accepted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
}
