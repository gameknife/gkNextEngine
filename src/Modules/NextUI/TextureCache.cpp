#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/TextureCache.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/StbImage.hpp"

#include <limits>
#include <vector>

namespace NextUI
{
    Runtime::FUiTextureHandle FTextureCache::Request(
        const std::string_view path,
        const bool srgb,
        const Runtime::EUiTextureLifetime lifetime,
        const std::function<uint64_t(std::string_view)>& resolveByName)
    {
        Runtime::FUiTextureHandle handle;
        if (path.empty())
        {
            return handle;
        }

        const std::string pathStr(path);
        if (!Utilities::FileHelper::IsAssetAvailable(pathStr))
        {
            return handle;
        }

        const Assets::ETextureLifetime textureLifetime = lifetime == Runtime::EUiTextureLifetime::Persistent
            ? Assets::ETextureLifetime::ETL_Persistent
            : Assets::ETextureLifetime::ETL_Transient;
        if (loadRequests_.insert(pathStr).second)
        {
            Assets::GlobalTexturePool::LoadTexture(pathStr, srgb, textureLifetime);
        }

        handle.id = resolveByName(pathStr);
        handle.valid = handle.id != 0;
        if (!handle.valid)
        {
            Assets::GlobalTexturePool::LoadTexture(pathStr, srgb, textureLifetime);
            handle.id = resolveByName(pathStr);
            handle.valid = handle.id != 0;
        }

        if (const auto found = pixelSizeCache_.find(pathStr); found != pixelSizeCache_.end())
        {
            handle.pixelSize = found->second;
            return handle;
        }

        int width = 0;
        int height = 0;
        int componentCount = 0;
        std::vector<uint8_t> imageData;
        const bool loaded = Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(pathStr, imageData);
        if (loaded && imageData.size() <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
            stbi_info_from_memory(imageData.data(), static_cast<int>(imageData.size()),
                                  &width, &height, &componentCount) != 0 &&
            width > 0 && height > 0)
        {
            handle.pixelSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
        }
        pixelSizeCache_[pathStr] = handle.pixelSize;
        return handle;
    }
}
