#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/GPU/HdrTextureCache.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#define M_NEXT_PI 3.14159265358979323846f

namespace Assets
{
    SphericalHarmonics ProjectHdrToSh(const float* hdrPixels, int width, int height)
    {
        SphericalHarmonics result{};

        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 9; ++j)
            {
                result.coefficients[i][j] = 0.0f;
            }
        }

        constexpr float shC0 = 0.282095f;
        constexpr float shC1 = 0.488603f;
        constexpr float shC2 = 1.092548f;
        constexpr float shC3 = 0.315392f;
        constexpr float shC4 = 0.546274f;

        for (int y = 0; y < height; ++y)
        {
            const float v = (y + 0.5f) / height;
            const float theta = v * M_NEXT_PI;
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);
            const float weight = sinTheta * (M_NEXT_PI / height) * (2.0f * M_NEXT_PI / width);

            for (int x = 0; x < width; ++x)
            {
                const float u = (x + 0.5f) / width;
                const float phi = u * 2.0f * M_NEXT_PI;
                const float sinPhi = std::sin(phi);
                const float cosPhi = std::cos(phi);

                // Match Common.SampleIBL's atan2(direction.x, direction.z) mapping.
                const float dx = sinTheta * sinPhi;
                const float dy = cosTheta;
                const float dz = sinTheta * cosPhi;

                float basis[9];
                basis[0] = shC0;
                basis[1] = -shC1 * dy;
                basis[2] = shC1 * dz;
                basis[3] = -shC1 * dx;
                basis[4] = shC2 * dx * dy;
                basis[5] = -shC2 * dy * dz;
                basis[6] = shC3 * (3.0f * dy * dy - 1.0f);
                basis[7] = -shC2 * dx * dz;
                basis[8] = shC4 * (dx * dx - dz * dz);

                const int pixelIndex = (y * width + x) * 4;
                const float r = hdrPixels[pixelIndex + 0];
                const float g = hdrPixels[pixelIndex + 1];
                const float b = hdrPixels[pixelIndex + 2];

                for (int i = 0; i < 9; ++i)
                {
                    result.coefficients[0][i] += r * basis[i] * weight;
                    result.coefficients[1][i] += g * basis[i] * weight;
                    result.coefficients[2][i] += b * basis[i] * weight;
                }
            }
        }

        return result;
    }

    const char* HDRResidencyName(GlobalTexturePool::EHDRTextureResidency residency)
    {
        return residency == GlobalTexturePool::EHDRTextureResidency::FullMip ? "full" : "lowest-mip";
    }

    std::string HDRCacheFileName(const std::string& textureName)
    {
        std::hash<std::string> hasher;
        return Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", hasher(textureName)), "texhdr");
    }

    std::unique_ptr<TextureImage> CreateHDRTextureImage(
        Vulkan::CommandPool& commandPool, const FHDRTexturePayload& payload,
        GlobalTexturePool::EHDRTextureResidency residency)
    {
        // A missing or undecodable HDR yields a default-constructed payload. Creating a
        // 0x0 VK_FORMAT_UNDEFINED image from it fails inside VMA and throws off a worker
        // thread, so report the failure and let the caller install its placeholder.
        if (payload.Width <= 0 || payload.Height <= 0
            || payload.Format == VK_FORMAT_UNDEFINED
            || payload.BasePixels.empty())
        {
            return nullptr;
        }

        const uint32_t baseSize = static_cast<uint32_t>(payload.BasePixels.size() * sizeof(float));
        if (residency == GlobalTexturePool::EHDRTextureResidency::FullMip
            && payload.MipLevelData.size() > 1
            && payload.MipDimensions.size() == payload.MipLevelData.size())
        {
            return std::make_unique<TextureImage>(
                commandPool, payload.Width, payload.Height, static_cast<uint32_t>(payload.MipLevelData.size()),
                payload.Format, reinterpret_cast<const unsigned char*>(payload.BasePixels.data()), baseSize,
                payload.MipLevelData, payload.MipDimensions);
        }

        if (residency == GlobalTexturePool::EHDRTextureResidency::LowestMip
            && payload.MipLevelData.size() > 1
            && payload.MipDimensions.size() == payload.MipLevelData.size()
            && !payload.MipLevelData.back().empty())
        {
            const auto& dim = payload.MipDimensions.back();
            const auto& mipData = payload.MipLevelData.back();
            const uint32_t mipSize = static_cast<uint32_t>(mipData.size() * sizeof(float));
            return std::make_unique<TextureImage>(
                commandPool, dim.first, dim.second, 1, payload.Format,
                reinterpret_cast<const unsigned char*>(mipData.data()), mipSize);
        }

        return std::make_unique<TextureImage>(
            commandPool, payload.Width, payload.Height, 1, payload.Format,
            reinterpret_cast<const unsigned char*>(payload.BasePixels.data()), baseSize);
    }
}
