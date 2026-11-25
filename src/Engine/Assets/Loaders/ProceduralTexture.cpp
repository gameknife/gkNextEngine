#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Assets/Loaders/ProceduralTexture.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"

#include <xxhash.h>

#include <mutex>
#include <unordered_map>

namespace Assets
{
    // Named (not anonymous) so unity builds cannot collide these helpers with
    // another translation unit's statics in the same batch.
    namespace ProceduralTextureDetail
    {
        std::mutex& AcquireMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<int32_t, uint64_t>& HashByTexture()
        {
            static std::unordered_map<int32_t, uint64_t> hashes;
            return hashes;
        }

        std::unordered_map<uint64_t, FProceduralTextureIds>& IdsByHash()
        {
            static std::unordered_map<uint64_t, FProceduralTextureIds> ids;
            return ids;
        }

        // Registers one bake, or finds it. sRGB for colour only: the normal and
        // MRA maps hold linear data. The name carries a hash of the pixels so a
        // bake that changes with the code (not with its parameters) changes its
        // pool key too.
        int32_t Register(GlobalTexturePool& pool, const std::string& baseName, const std::vector<uint8_t>& pixels,
                         uint32_t size, bool srgb)
        {
            const std::string name = fmt::format("{}-{:016x}", baseName, XXH64(pixels.data(), pixels.size(), 0));
            if (const uint32_t existing = pool.TryGetTextureIndex(name); existing != static_cast<uint32_t>(-1))
            {
                return static_cast<int32_t>(existing);
            }
            uint32_t index = static_cast<uint32_t>(-1);
            if (pool.IsOwnerThread())
            {
                Vulkan::CommandPool& commandPool = pool.GetMainThreadCommandPool();
                auto image = std::make_unique<TextureImage>(commandPool, size, size, 1,
                                                            srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
                                                            pixels.data(), static_cast<uint32_t>(pixels.size()));
                image->MainThreadPostLoading(commandPool);
                index = pool.RegisterTexture(name, std::move(image), ETextureLifetime::ETL_Persistent);
            }
            else
            {
                index = pool.RequestNewRawTextureAsync(name, size, size, srgb, pixels.data(), pixels.size(),
                                                       ETextureLifetime::ETL_Persistent);
            }
            return index == static_cast<uint32_t>(-1) ? -1 : static_cast<int32_t>(index);
        }
    }

    namespace ProceduralTexture
    {
        using namespace ProceduralTextureDetail;

        FProceduralTextureIds AcquireMaps(const FProceduralTexture& texture)
        {
            FProceduralTextureIds ids;
            GlobalTexturePool* pool = GlobalTexturePool::GetInstance();
            if (!pool || !texture.graph)
            {
                return ids;
            }
            std::scoped_lock lock(AcquireMutex());
            const uint64_t hash = texture.Hash();
            const std::string base = fmt::format("procedural/{}-{:016x}", texture.pattern, hash);
            // The pool owns the images (persistent lifetime); the id cache only
            // skips re-baking a print this process has already registered.
            if (const auto found = IdsByHash().find(hash); found != IdsByHash().end())
            {
                if (found->second.albedo >= 0 &&
                    static_cast<uint32_t>(found->second.albedo) < pool->TotalTextures())
                {
                    return found->second;
                }
                IdsByHash().erase(found);
            }

            const FProceduralMaps maps = BakeMaps(texture, kDefaultSize);
            ids.albedo = Register(*pool, base, maps.albedo, kDefaultSize, true);
            if (ids.albedo < 0)
            {
                return ids;
            }
            if (maps.hasMra) ids.mra = Register(*pool, base + "-mra", maps.mra, kDefaultSize, false);
            if (maps.hasNormal) ids.normal = Register(*pool, base + "-normal", maps.normal, kDefaultSize, false);
            HashByTexture()[ids.albedo] = hash;
            IdsByHash()[hash] = ids;
            return ids;
        }

        int32_t Acquire(const FProceduralTexture& texture)
        {
            return AcquireMaps(texture).albedo;
        }

        uint64_t ContentHashOfTexture(int32_t textureId)
        {
            std::scoped_lock lock(AcquireMutex());
            const auto found = HashByTexture().find(textureId);
            return found == HashByTexture().end() ? 0 : found->second;
        }
    }
}
