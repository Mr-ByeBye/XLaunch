#include "platform/FileDropTarget.h"

#include <shellapi.h>

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

    bool FileDropTarget::SupportsFiles(IDataObject* dataObject) const
    {
        if (dataObject == nullptr)
            return false;
        FORMATETC format = FileDropFormat();
        return SUCCEEDED(dataObject->QueryGetData(&format));
    }

    std::vector<std::wstring> FileDropTarget::ReadFiles(IDataObject* dataObject) const
    {
        std::vector<std::wstring> paths;
        FORMATETC format = FileDropFormat();
        STGMEDIUM medium{};
        if (dataObject == nullptr || FAILED(dataObject->GetData(&format, &medium)))
            return paths;

        const HDROP drop = static_cast<HDROP>(medium.hGlobal);
        if (drop != nullptr)
        {
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            paths.reserve(count);
            for (UINT index = 0; index < count; ++index)
            {
                const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                std::wstring path(length + 1, L'\0');
                DragQueryFileW(drop, index, path.data(), static_cast<UINT>(path.size()));
                path.resize(length);
                paths.push_back(std::move(path));
            }
        }
        ReleaseStgMedium(&medium);
        return paths;
    }

    HRESULT FileDropTarget::DragEnter(IDataObject* dataObject, DWORD, POINTL, DWORD* effect)
    {
        acceptsCurrentDrag_ = SupportsFiles(dataObject);
        if (effect != nullptr)
            *effect = acceptsCurrentDrag_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (dragStateCallback_)
            dragStateCallback_(acceptsCurrentDrag_);
        return S_OK;
    }

    HRESULT FileDropTarget::DragOver(DWORD, POINTL, DWORD* effect)
    {
        if (effect != nullptr)
            *effect = acceptsCurrentDrag_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    HRESULT FileDropTarget::DragLeave()
    {
        acceptsCurrentDrag_ = false;
        if (dragStateCallback_)
            dragStateCallback_(false);
        return S_OK;
    }

    HRESULT FileDropTarget::Drop(IDataObject* dataObject, DWORD, POINTL, DWORD* effect)
    {
        std::vector<std::wstring> paths = ReadFiles(dataObject);
        const bool accepted = acceptsCurrentDrag_ && !paths.empty();
        acceptsCurrentDrag_ = false;
        if (dragStateCallback_)
            dragStateCallback_(false);
        if (accepted && dropCallback_)
            dropCallback_(std::move(paths));
        if (effect != nullptr)
            *effect = accepted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
}
