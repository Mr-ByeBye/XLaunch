#pragma once

#include "core/LauncherData.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

#include <d3d11.h>
#include <wrl/client.h>

namespace xlaunch
{
    struct CachedIcon
    {
        ID3D11ShaderResourceView* texture = nullptr;
        bool fallback = false;
    };

    class IconCache
    {
    public:
        explicit IconCache(ID3D11Device* device);

        [[nodiscard]] CachedIcon Get(const LaunchItem& item, int pixelSize);
        void Invalidate(const std::string& itemId);
        void Prune(const std::unordered_set<std::string>& activeItemIds);
        void Clear();

    private:
        struct Entry
        {
            std::string signature;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;
            bool fallback = false;
        };

        [[nodiscard]] std::string MakeSignature(const LaunchItem& item, int pixelSize) const;
        [[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CreateTexture(HICON icon, int pixelSize) const;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        std::unordered_map<std::string, Entry> entries_;
    };
}
