#include "renderer/IconCache.h"

#include "platform/ShellIcon.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace xlaunch
{
    namespace
    {
        constexpr std::array<int, 5> kCachedSizes{ 32, 40, 48, 56, 64 };
        constexpr std::uint32_t kCacheMagic = 0x43494C58; // XLIC
        constexpr std::uint32_t kCacheVersion = 4;

        std::string FileStamp(const std::string& value)
        {
            if (value.empty() || value.starts_with("shell:")) return {};
            std::error_code error;
            const std::u8string utf8(reinterpret_cast<const char8_t*>(value.data()), value.size());
            const std::filesystem::path path(utf8);
            if (!std::filesystem::exists(path, error)) return {};
            const auto size = std::filesystem::is_regular_file(path, error) ? std::filesystem::file_size(path, error) : 0;
            const auto time = std::filesystem::last_write_time(path, error).time_since_epoch().count();
            return std::to_string(size) + ':' + std::to_string(time);
        }

        std::string HashId(const std::string& value)
        {
            std::uint64_t hash = 14695981039346656037ull;
            for (const unsigned char character : value) { hash ^= character; hash *= 1099511628211ull; }
            std::ostringstream stream;
            stream << std::hex << std::setw(16) << std::setfill('0') << hash;
            return stream.str();
        }
    }

    IconCache::IconCache(ID3D11Device* device, std::filesystem::path cacheDirectory)
        : device_(device), cacheDirectory_(std::move(cacheDirectory))
    {
        worker_ = std::thread([this] { WorkerMain(); });
    }

    IconCache::~IconCache()
    {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        condition_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    std::string IconCache::MakeSignature(const LaunchItem& item, int pixelSize) const
    {
        return "4\n" + item.target + '\n' + item.customIconPath + '\n' + ItemTypeName(item.type) + '\n' +
            std::to_string(pixelSize) + '\n' + FileStamp(item.target) + '\n' + FileStamp(item.customIconPath);
    }

    std::string IconCache::MakeCacheKey(const std::string& itemId, int pixelSize) const
    {
        return itemId + '\n' + std::to_string(pixelSize);
    }

    std::filesystem::path IconCache::ItemCacheDirectory(const std::string& itemId) const
    {
        return cacheDirectory_ / HashId(itemId);
    }

    std::filesystem::path IconCache::CacheFile(const std::string& itemId, int pixelSize) const
    {
        return ItemCacheDirectory(itemId) / (std::to_string(pixelSize) + ".xic");
    }

    std::vector<std::uint8_t> IconCache::RasterizeIcon(HICON icon, int pixelSize) const
    {
        std::vector<std::uint8_t> pixels;
        if (icon == nullptr || pixelSize <= 0) return pixels;

        int sourceSize = pixelSize;
        ICONINFO iconInfo{};
        if (GetIconInfo(icon, &iconInfo))
        {
            BITMAP nativeBitmap{};
            if (iconInfo.hbmColor && GetObjectW(iconInfo.hbmColor, sizeof(nativeBitmap), &nativeBitmap) != 0)
                sourceSize = (std::max)(nativeBitmap.bmWidth, nativeBitmap.bmHeight);
            else if (iconInfo.hbmMask && GetObjectW(iconInfo.hbmMask, sizeof(nativeBitmap), &nativeBitmap) != 0)
                sourceSize = (std::max)(nativeBitmap.bmWidth, nativeBitmap.bmHeight / 2);
            if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
            if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
        }
        sourceSize = std::clamp(sourceSize, pixelSize, 512);

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = sourceSize;
        info.bmiHeader.biHeight = -sourceSize;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        HDC context = CreateCompatibleDC(nullptr);
        if (!bitmap || !context || !bits) { if (context) DeleteDC(context); if (bitmap) DeleteObject(bitmap); return pixels; }
        const HGDIOBJ old = SelectObject(context, bitmap);
        const std::size_t sourceByteCount = static_cast<std::size_t>(sourceSize) * sourceSize * 4;
        std::fill_n(static_cast<std::uint8_t*>(bits), sourceByteCount, 0);
        DrawIconEx(context, 0, 0, icon, sourceSize, sourceSize, 0, nullptr, DI_NORMAL);
        auto* source = static_cast<std::uint8_t*>(bits);
        bool hasAlpha = false;
        for (int i = 0; i < sourceSize * sourceSize; ++i) hasAlpha |= source[i * 4 + 3] != 0;
        if (!hasAlpha) for (int i = 0; i < sourceSize * sourceSize; ++i)
            source[i * 4 + 3] = (std::max)({ source[i * 4], source[i * 4 + 1], source[i * 4 + 2] });

        const std::size_t targetByteCount = static_cast<std::size_t>(pixelSize) * pixelSize * 4;
        if (sourceSize == pixelSize)
            pixels.assign(source, source + targetByteCount);
        else
        {
            pixels.resize(targetByteCount);
            const float scale = static_cast<float>(sourceSize) / pixelSize;
            for (int y = 0; y < pixelSize; ++y)
            {
                const float sourceY = (y + 0.5f) * scale - 0.5f;
                const float sourceYFloor = std::floor(sourceY);
                const int y0 = std::clamp(static_cast<int>(sourceYFloor), 0, sourceSize - 1);
                const int y1 = (std::min)(y0 + 1, sourceSize - 1);
                const float fy = sourceY - sourceYFloor;
                for (int x = 0; x < pixelSize; ++x)
                {
                    const float sourceX = (x + 0.5f) * scale - 0.5f;
                    const float sourceXFloor = std::floor(sourceX);
                    const int x0 = std::clamp(static_cast<int>(sourceXFloor), 0, sourceSize - 1);
                    const int x1 = (std::min)(x0 + 1, sourceSize - 1);
                    const float fx = sourceX - sourceXFloor;
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        const float top = source[(y0 * sourceSize + x0) * 4 + channel] * (1.0f - fx) +
                            source[(y0 * sourceSize + x1) * 4 + channel] * fx;
                        const float bottom = source[(y1 * sourceSize + x0) * 4 + channel] * (1.0f - fx) +
                            source[(y1 * sourceSize + x1) * 4 + channel] * fx;
                        pixels[(y * pixelSize + x) * 4 + channel] = static_cast<std::uint8_t>(
                            std::clamp(top * (1.0f - fy) + bottom * fy, 0.0f, 255.0f));
                    }
                }
            }
        }
        SelectObject(context, old); DeleteDC(context); DeleteObject(bitmap);
        return pixels;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> IconCache::CreateTexture(const std::vector<std::uint8_t>& pixels, int pixelSize) const
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        if (!device_ || pixelSize <= 0 || pixels.size() != static_cast<std::size_t>(pixelSize) * pixelSize * 4) return view;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = pixelSize; description.Height = pixelSize; description.MipLevels = 1; description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM; description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE; description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{ pixels.data(), static_cast<UINT>(pixelSize * 4), 0 };
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (SUCCEEDED(device_->CreateTexture2D(&description, &data, &texture))) device_->CreateShaderResourceView(texture.Get(), nullptr, &view);
        return view;
    }

    bool IconCache::LoadDiskCache(const LoadRequest& request, LoadResult& result) const
    {
        if (cacheDirectory_.empty()) return false;
        std::ifstream input(CacheFile(request.item.id, request.pixelSize), std::ios::binary);
        std::uint32_t magic{}, version{}, size{}, fallback{}, signatureLength{}, pixelCount{};
        input.read(reinterpret_cast<char*>(&magic), 4); input.read(reinterpret_cast<char*>(&version), 4);
        input.read(reinterpret_cast<char*>(&size), 4); input.read(reinterpret_cast<char*>(&fallback), 4);
        input.read(reinterpret_cast<char*>(&signatureLength), 4); input.read(reinterpret_cast<char*>(&pixelCount), 4);
        const std::size_t expected = static_cast<std::size_t>(request.pixelSize) * request.pixelSize * 4;
        if (!input || magic != kCacheMagic || version != kCacheVersion || size != request.pixelSize ||
            signatureLength > 1024 * 1024 || pixelCount != expected) return false;
        std::string signature(signatureLength, '\0'); input.read(signature.data(), signature.size());
        if (!input || signature != request.signature) return false;
        result = { request.item.id, request.signature, request.pixelSize, std::vector<std::uint8_t>(pixelCount), fallback != 0 };
        input.read(reinterpret_cast<char*>(result.pixels.data()), result.pixels.size());
        return static_cast<bool>(input);
    }

    void IconCache::SaveDiskCache(const LoadResult& result) const
    {
        if (cacheDirectory_.empty() || result.pixels.empty()) return;
        std::error_code error;
        const auto directory = ItemCacheDirectory(result.itemId);
        std::filesystem::create_directories(directory, error);
        if (error) return;
        const auto destination = CacheFile(result.itemId, result.pixelSize);
        auto temporary = destination; temporary += ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        const std::uint32_t values[]{ kCacheMagic, kCacheVersion, static_cast<std::uint32_t>(result.pixelSize),
            result.fallback ? 1u : 0u, static_cast<std::uint32_t>(result.signature.size()), static_cast<std::uint32_t>(result.pixels.size()) };
        output.write(reinterpret_cast<const char*>(values), sizeof(values)); output.write(result.signature.data(), result.signature.size());
        output.write(reinterpret_cast<const char*>(result.pixels.data()), result.pixels.size()); output.close();
        if (!output) { std::filesystem::remove(temporary, error); return; }
        std::filesystem::remove(destination, error); error.clear(); std::filesystem::rename(temporary, destination, error);
        if (error) std::filesystem::remove(temporary, error);
    }

    void IconCache::RemoveDiskCache(const std::string& itemId) const
    {
        if (cacheDirectory_.empty()) return;
        std::error_code error; std::filesystem::remove_all(ItemCacheDirectory(itemId), error);
    }

    CachedIcon IconCache::Get(const LaunchItem& item, int pixelSize)
    {
        const std::string key = MakeCacheKey(item.id, pixelSize);
        const auto known = knownSignatures_.find(key);
        const std::string signature = known != knownSignatures_.end() ? known->second : MakeSignature(item, pixelSize);
        knownSignatures_[key] = signature;
        if (const auto found = entries_.find(key); found != entries_.end() && found->second.signature == signature)
            return { found->second.texture.Get(), found->second.fallback };
        LoadResult result; bool ready = false, queued = false;
        { std::lock_guard lock(mutex_);
          if (auto found = completed_.find(key); found != completed_.end() && found->second.signature == signature)
          { result = std::move(found->second); completed_.erase(found); ready = true; }
          if (auto found = queuedSignatures_.find(key); found != queuedSignatures_.end() && found->second == signature) queued = true; }
        auto nearestLoaded = [&]() -> CachedIcon
        {
            const std::string prefix = item.id + '\n';
            Entry* nearest = nullptr;
            int nearestDistance = (std::numeric_limits<int>::max)();
            for (auto& [entryKey, entry] : entries_)
            {
                if (!entryKey.starts_with(prefix)) continue;
                const int entrySize = std::atoi(entryKey.c_str() + prefix.size());
                const auto expected = knownSignatures_.find(entryKey);
                if (expected == knownSignatures_.end() || entry.signature != expected->second) continue;
                const int distance = std::abs(entrySize - pixelSize);
                if (distance < nearestDistance) { nearest = &entry; nearestDistance = distance; }
            }
            return nearest ? CachedIcon{ nearest->texture.Get(), nearest->fallback } : CachedIcon{};
        };
        if (!ready && queued)
        {
            Queue(item, pixelSize, signature, true);
            return nearestLoaded();
        }
        if (!ready)
        {
            // Production never performs Shell or disk I/O on the render thread.
            // A nearby texture remains visible until the requested size is ready.
            if (!cacheDirectory_.empty())
            {
                Queue(item, pixelSize, signature, true);
                return nearestLoaded();
            }
            const LoadRequest request{ item, pixelSize, signature };
            if (!LoadDiskCache(request, result))
            {
                ShellIconResult shell = LoadShellIcon(item, pixelSize);
                result = { item.id, signature, pixelSize, RasterizeIcon(shell.icon, pixelSize), shell.usedFallback };
                if (shell.icon) DestroyIcon(shell.icon);
                SaveDiskCache(result);
            }
        }
        Entry entry{ signature, CreateTexture(result.pixels, pixelSize), result.fallback };
        entries_[key] = std::move(entry); const Entry& stored = entries_.at(key);
        return { stored.texture.Get(), stored.fallback };
    }

    void IconCache::Prefetch(const AppConfig& config, int pixelSize, bool highPriority)
    {
        for (const Category& category : config.categories) for (const LaunchItem& item : category.items)
        { const auto key = MakeCacheKey(item.id, pixelSize);
          const auto known = knownSignatures_.find(key);
          const std::string signature = known != knownSignatures_.end() ? known->second : MakeSignature(item, pixelSize);
          knownSignatures_[key] = signature;
          if (auto found = entries_.find(key); found == entries_.end() || found->second.signature != signature)
              Queue(item, pixelSize, signature, highPriority); }
    }

    void IconCache::PrefetchAllSizes(const AppConfig& config, float dpiScale)
    {
        // The visible size goes first so an existing user's one-time disk-cache
        // migration never delays the icons that are needed on screen right now.
        const auto scaled = [dpiScale](int size) { return (std::max)(1, static_cast<int>(std::lround(size * dpiScale))); };
        Prefetch(config, scaled(config.appearance.iconSize), true);
        for (int size : kCachedSizes)
            if (size != config.appearance.iconSize)
                Prefetch(config, scaled(size));
        // Compact lists always render 24 px icons. Keep that size in the same
        // persistent cache instead of waiting for the first compact-mode frame.
        Prefetch(config, scaled(24));
    }

    void IconCache::Queue(const LaunchItem& item, int pixelSize, const std::string& signature, bool highPriority)
    {
        const auto key = MakeCacheKey(item.id, pixelSize);
        { std::lock_guard lock(mutex_);
          if (auto found = queuedSignatures_.find(key); found != queuedSignatures_.end() && found->second == signature)
          {
              if (highPriority)
              {
                  const auto request = std::find_if(requests_.begin(), requests_.end(), [&](const LoadRequest& queued)
                      { return MakeCacheKey(queued.item.id, queued.pixelSize) == key && queued.signature == signature; });
                  if (request != requests_.end() && request != requests_.begin())
                  { LoadRequest promoted = std::move(*request); requests_.erase(request); requests_.push_front(std::move(promoted)); }
              }
              return;
          }
          if (auto found = completed_.find(key); found != completed_.end() && found->second.signature == signature) return;
          queuedSignatures_[key] = signature;
          if (highPriority) requests_.push_front({ item, pixelSize, signature });
          else requests_.push_back({ item, pixelSize, signature }); }
        condition_.notify_one();
    }

    void IconCache::WorkerMain()
    {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        for (;;)
        {
            LoadRequest request;
            { std::unique_lock lock(mutex_); condition_.wait(lock, [&] { return stopping_ || !requests_.empty(); });
              if (stopping_) break; request = std::move(requests_.front()); requests_.pop_front(); }
            LoadResult result;
            if (!LoadDiskCache(request, result))
            { ShellIconResult shell = LoadShellIcon(request.item, request.pixelSize);
              result = { request.item.id, request.signature, request.pixelSize, RasterizeIcon(shell.icon, request.pixelSize), shell.usedFallback };
              if (shell.icon) DestroyIcon(shell.icon); }
            const auto key = MakeCacheKey(request.item.id, request.pixelSize);
            bool stillWanted = false;
            { std::lock_guard lock(mutex_);
              if (auto current = queuedSignatures_.find(key); current != queuedSignatures_.end() && current->second == request.signature)
                  stillWanted = true; }
            if (!stillWanted) continue;

            // File-system work must never hold the mutex used by the render thread.
            SaveDiskCache(result);
            bool discarded = false;
            { std::lock_guard lock(mutex_);
              if (auto current = queuedSignatures_.find(key); current != queuedSignatures_.end() && current->second == request.signature)
              { completed_[key] = std::move(result); queuedSignatures_.erase(current); }
              else discarded = true; }
            if (discarded) RemoveDiskCache(request.item.id);
        }
        if (SUCCEEDED(com)) CoUninitialize();
    }

    void IconCache::Invalidate(const std::string& itemId)
    {
        for (auto it = entries_.begin(); it != entries_.end();) it = it->first.starts_with(itemId + '\n') ? entries_.erase(it) : std::next(it);
        for (auto it = knownSignatures_.begin(); it != knownSignatures_.end();) it = it->first.starts_with(itemId + '\n') ? knownSignatures_.erase(it) : std::next(it);
        std::lock_guard lock(mutex_);
        for (auto it = queuedSignatures_.begin(); it != queuedSignatures_.end();) it = it->first.starts_with(itemId + '\n') ? queuedSignatures_.erase(it) : std::next(it);
        for (auto it = completed_.begin(); it != completed_.end();) it = it->first.starts_with(itemId + '\n') ? completed_.erase(it) : std::next(it);
        RemoveDiskCache(itemId);
    }

    void IconCache::Prune(const std::unordered_set<std::string>& activeItemIds)
    {
        std::unordered_set<std::string> removed;
        for (auto it = entries_.begin(); it != entries_.end();) { const auto id = it->first.substr(0, it->first.find('\n'));
            if (!activeItemIds.contains(id)) { removed.insert(id); it = entries_.erase(it); } else ++it; }
        for (auto it = knownSignatures_.begin(); it != knownSignatures_.end();) { const auto id = it->first.substr(0, it->first.find('\n'));
            if (!activeItemIds.contains(id)) it = knownSignatures_.erase(it); else ++it; }
        { std::lock_guard lock(mutex_);
          for (auto it = queuedSignatures_.begin(); it != queuedSignatures_.end();) { const auto id = it->first.substr(0, it->first.find('\n')); if (!activeItemIds.contains(id)) { removed.insert(id); it = queuedSignatures_.erase(it); } else ++it; }
          for (auto it = completed_.begin(); it != completed_.end();) { if (!activeItemIds.contains(it->second.itemId)) { removed.insert(it->second.itemId); it = completed_.erase(it); } else ++it; } }
        for (const auto& id : removed) RemoveDiskCache(id);
    }

    void IconCache::Clear()
    {
        entries_.clear(); knownSignatures_.clear(); std::lock_guard lock(mutex_); requests_.clear(); queuedSignatures_.clear(); completed_.clear();
    }
}
