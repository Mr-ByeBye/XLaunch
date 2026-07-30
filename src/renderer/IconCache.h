#pragma once

#include "core/LauncherData.h"

#include <string>
#include <filesystem>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
        explicit IconCache(ID3D11Device* device, std::filesystem::path cacheDirectory = {});
        ~IconCache();

        [[nodiscard]] CachedIcon Get(const LaunchItem& item, int pixelSize);
        void Prefetch(const AppConfig& config, int pixelSize);
        void PrefetchAllSizes(const AppConfig& config);
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

        struct LoadRequest
        {
            LaunchItem item;
            int pixelSize = 0;
            std::string signature;
        };

        struct LoadResult
        {
            std::string itemId;
            std::string signature;
            int pixelSize = 0;
            std::vector<std::uint8_t> pixels;
            bool fallback = false;
        };

        [[nodiscard]] std::string MakeSignature(const LaunchItem& item, int pixelSize) const;
        [[nodiscard]] std::string MakeCacheKey(const std::string& itemId, int pixelSize) const;
        [[nodiscard]] std::filesystem::path ItemCacheDirectory(const std::string& itemId) const;
        [[nodiscard]] std::filesystem::path CacheFile(const std::string& itemId, int pixelSize) const;
        [[nodiscard]] std::vector<std::uint8_t> RasterizeIcon(HICON icon, int pixelSize) const;
        [[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CreateTexture(
            const std::vector<std::uint8_t>& pixels, int pixelSize) const;
        [[nodiscard]] bool LoadDiskCache(const LoadRequest& request, LoadResult& result) const;
        void SaveDiskCache(const LoadResult& result) const;
        void RemoveDiskCache(const std::string& itemId) const;
        void Queue(const LaunchItem& item, int pixelSize, const std::string& signature);
        void WorkerMain();

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        std::filesystem::path cacheDirectory_;
        std::unordered_map<std::string, Entry> entries_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<LoadRequest> requests_;
        std::unordered_map<std::string, std::string> queuedSignatures_;
        std::unordered_map<std::string, LoadResult> completed_;
        std::thread worker_;
        bool stopping_ = false;
    };
}
