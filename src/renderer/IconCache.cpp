#include "renderer/IconCache.h"

#include "platform/ShellIcon.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace xlaunch
{
    IconCache::IconCache(ID3D11Device* device)
        : device_(device)
    {
    }

    std::string IconCache::MakeSignature(const LaunchItem& item, int pixelSize) const
    {
        return item.target + '\n' + item.customIconPath + '\n' + ItemTypeName(item.type) + '\n' + std::to_string(pixelSize);
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> IconCache::CreateTexture(HICON icon, int pixelSize) const
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        if (icon == nullptr || device_ == nullptr || pixelSize <= 0)
            return view;

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = pixelSize;
        bitmapInfo.bmiHeader.biHeight = -pixelSize;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
        HDC deviceContext = CreateCompatibleDC(nullptr);
        if (bitmap == nullptr || deviceContext == nullptr || bits == nullptr)
        {
            if (deviceContext != nullptr)
                DeleteDC(deviceContext);
            if (bitmap != nullptr)
                DeleteObject(bitmap);
            return view;
        }

        HGDIOBJ oldBitmap = SelectObject(deviceContext, bitmap);
        std::fill_n(static_cast<std::uint32_t*>(bits), static_cast<std::size_t>(pixelSize) * pixelSize, 0u);
        DrawIconEx(deviceContext, 0, 0, icon, pixelSize, pixelSize, 0, nullptr, DI_NORMAL);

        auto* pixels = static_cast<std::uint8_t*>(bits);
        bool hasAlpha = false;
        for (int index = 0; index < pixelSize * pixelSize; ++index)
            hasAlpha |= pixels[index * 4 + 3] != 0;
        if (!hasAlpha)
        {
            for (int index = 0; index < pixelSize * pixelSize; ++index)
            {
                std::uint8_t* pixel = pixels + index * 4;
                pixel[3] = (std::max)({ pixel[0], pixel[1], pixel[2] });
            }
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(pixelSize);
        description.Height = static_cast<UINT>(pixelSize);
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = bits;
        data.SysMemPitch = static_cast<UINT>(pixelSize * 4);

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (SUCCEEDED(device_->CreateTexture2D(&description, &data, &texture)))
            device_->CreateShaderResourceView(texture.Get(), nullptr, &view);

        SelectObject(deviceContext, oldBitmap);
        DeleteDC(deviceContext);
        DeleteObject(bitmap);
        return view;
    }

    CachedIcon IconCache::Get(const LaunchItem& item, int pixelSize)
    {
        const std::string signature = MakeSignature(item, pixelSize);
        const auto existing = entries_.find(item.id);
        if (existing != entries_.end() && existing->second.signature == signature)
            return { existing->second.texture.Get(), existing->second.fallback };

        ShellIconResult shellIcon = LoadShellIcon(item, pixelSize);
        Entry entry;
        entry.signature = signature;
        entry.fallback = shellIcon.usedFallback;
        entry.texture = CreateTexture(shellIcon.icon, pixelSize);
        if (shellIcon.icon != nullptr)
            DestroyIcon(shellIcon.icon);
        entries_[item.id] = std::move(entry);
        const Entry& stored = entries_.at(item.id);
        return { stored.texture.Get(), stored.fallback };
    }

    void IconCache::Invalidate(const std::string& itemId)
    {
        entries_.erase(itemId);
    }

    void IconCache::Prune(const std::unordered_set<std::string>& activeItemIds)
    {
        for (auto iterator = entries_.begin(); iterator != entries_.end();)
        {
            if (!activeItemIds.contains(iterator->first))
                iterator = entries_.erase(iterator);
            else
                ++iterator;
        }
    }

    void IconCache::Clear()
    {
        entries_.clear();
    }
}
