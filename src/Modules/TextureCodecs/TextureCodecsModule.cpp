#include "Modules/TextureCodecs/TextureCodecsModule.hpp"

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/GPU/HdrTextureCache.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/StbImage.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "ThirdParty/lzav/lzav.h"

#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <ktx.h>
#include <nlohmann/json.hpp>
#include <webp/decode.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <system_error>

#define M_NEXT_PI 3.14159265358979323846f

namespace
{
    constexpr uint32_t kHdrCacheMagic = 0x48445243; // 'HDRC'
    constexpr uint32_t kHdrCacheVersion = 2;

    struct HdrCacheHeader
    {
        uint32_t magic;
        uint32_t version;
        uint64_t originalSize;
        uint64_t compressedSize;
        uint64_t dataHash;
    };

    uint64_t HashBuffer(const uint8_t* data, size_t size)
    {
        constexpr uint64_t fnvOffset = 1469598103934665603ull;
        constexpr uint64_t fnvPrime = 1099511628211ull;

        uint64_t hash = fnvOffset;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= static_cast<uint64_t>(data[i]);
            hash *= fnvPrime;
        }

        return hash;
    }

    void PrefilterEnvironmentMapLevel(const float* sourcePixels, int sourceWidth, int sourceHeight,
                                      float* targetPixels, int targetWidth, int targetHeight,
                                      float roughness)
    {
        const int sampleCount = std::max(1, static_cast<int>(8 * (1.0f - roughness) + 16 * roughness));

        for (int y = 0; y < targetHeight; ++y)
        {
            for (int x = 0; x < targetWidth; ++x)
            {
                const float u = (x + 0.5f) / targetWidth;
                const float v = (y + 0.5f) / targetHeight;

                const float theta = v * M_NEXT_PI;
                const float phi = u * 2.0f * M_NEXT_PI;

                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);
                const float sinPhi = std::sin(phi);
                const float cosPhi = std::cos(phi);

                const float mainDirX = sinTheta * cosPhi;
                const float mainDirY = cosTheta;
                const float mainDirZ = sinTheta * sinPhi;

                float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
                if (std::abs(mainDirY) > 0.999f)
                {
                    upX = 1.0f;
                    upY = 0.0f;
                    upZ = 0.0f;
                }

                float tangentX = upY * mainDirZ - upZ * mainDirY;
                float tangentY = upZ * mainDirX - upX * mainDirZ;
                float tangentZ = upX * mainDirY - upY * mainDirX;

                const float tangentLen = std::sqrt(tangentX * tangentX + tangentY * tangentY + tangentZ * tangentZ);
                tangentX /= tangentLen;
                tangentY /= tangentLen;
                tangentZ /= tangentLen;

                const float bitangentX = mainDirY * tangentZ - mainDirZ * tangentY;
                const float bitangentY = mainDirZ * tangentX - mainDirX * tangentZ;
                const float bitangentZ = mainDirX * tangentY - mainDirY * tangentX;

                float colorR = 0.0f, colorG = 0.0f, colorB = 0.0f;
                float totalWeight = 0.0f;

                for (int i = 0; i < sampleCount; ++i)
                {
                    const float xi1 = static_cast<float>(i) / sampleCount;
                    const float xi2 = static_cast<float>((i * 17 + 13) % sampleCount) / sampleCount;

                    const float alpha = roughness * roughness;
                    const float alpha2 = alpha * alpha;

                    const float sampleCosTheta = std::sqrt((1.0f - xi1) / (1.0f + (alpha2 - 1.0f) * xi1));
                    const float sampleSinTheta = std::sqrt(1.0f - sampleCosTheta * sampleCosTheta);
                    const float localPhi = 2.0f * M_NEXT_PI * xi2;

                    const float localX = sampleSinTheta * std::cos(localPhi);
                    const float localY = sampleSinTheta * std::sin(localPhi);
                    const float localZ = sampleCosTheta;

                    const float worldX = localX * tangentX + localY * bitangentX + localZ * mainDirX;
                    const float worldY = localX * tangentY + localY * bitangentY + localZ * mainDirY;
                    const float worldZ = localX * tangentZ + localY * bitangentZ + localZ * mainDirZ;

                    const float sampleTheta = std::acos(std::clamp(worldY, -1.0f, 1.0f));
                    float samplePhi = std::atan2(worldZ, worldX);
                    if (samplePhi < 0)
                    {
                        samplePhi += 2.0f * M_NEXT_PI;
                    }

                    const float sampleU = samplePhi / (2.0f * M_NEXT_PI);
                    const float sampleV = sampleTheta / M_NEXT_PI;

                    const int sampleX = static_cast<int>(sampleU * sourceWidth) % sourceWidth;
                    const int sampleY = static_cast<int>(sampleV * sourceHeight) % sourceHeight;

                    const int sampleIndex = (sampleY * sourceWidth + sampleX) * 4;

                    constexpr float weight = 1.0f;
                    colorR += sourcePixels[sampleIndex + 0] * weight;
                    colorG += sourcePixels[sampleIndex + 1] * weight;
                    colorB += sourcePixels[sampleIndex + 2] * weight;
                    totalWeight += weight;
                }

                if (totalWeight > 0.0f)
                {
                    colorR /= totalWeight;
                    colorG /= totalWeight;
                    colorB /= totalWeight;
                }

                const int targetIndex = (y * targetWidth + x) * 4;
                targetPixels[targetIndex + 0] = colorR;
                targetPixels[targetIndex + 1] = colorG;
                targetPixels[targetIndex + 2] = colorB;
                targetPixels[targetIndex + 3] = 1.0f;
            }
        }
    }

    void PrefilterHdrEnvironmentMap(const float* hdrPixels, int width, int height,
                                    std::vector<std::vector<float>>& mipLevels,
                                    std::vector<std::pair<int, int>>& mipDimensions)
    {
        constexpr int maxMipLevels = 8;
        mipLevels.clear();
        mipDimensions.clear();

        int currentWidth = width;
        int currentHeight = height;

        for (int mipLevel = 0; mipLevel < maxMipLevels; ++mipLevel)
        {
            if (currentWidth < 4 || currentHeight < 4)
            {
                break;
            }

            mipDimensions.push_back({currentWidth, currentHeight});
            mipLevels.emplace_back(currentWidth * currentHeight * 4);

            if (mipLevel > 0)
            {
                const float roughness = static_cast<float>(mipLevel) / (maxMipLevels - 1);
                PrefilterEnvironmentMapLevel(hdrPixels, width, height,
                                             mipLevels[mipLevel].data(),
                                             currentWidth, currentHeight, roughness);
            }

            currentWidth = std::max(1, currentWidth / 2);
            currentHeight = std::max(1, currentHeight / 2);
        }
    }

    bool LoadHDRTexturePayloadFromCache(const std::string& textureName, Assets::FHDRTexturePayload& payload)
    {
        const std::string cacheFileName = Assets::HDRCacheFileName(textureName);
        std::filesystem::path cacheFilePath(cacheFileName);
        if (!std::filesystem::exists(cacheFilePath))
        {
            return false;
        }

        bool validCache = false;
        std::ifstream cacheFile(cacheFileName, std::ios::binary);
        if (cacheFile.is_open())
        {
            HdrCacheHeader header{};
            cacheFile.read(reinterpret_cast<char*>(&header), sizeof(header));

            validCache = cacheFile.gcount() == sizeof(header)
                && header.magic == kHdrCacheMagic
                && header.version == kHdrCacheVersion
                && header.originalSize > 0
                && header.compressedSize > 0
                && header.originalSize <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())
                && header.compressedSize <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())
                && header.originalSize <= static_cast<uint64_t>(std::numeric_limits<int>::max())
                && header.compressedSize <= static_cast<uint64_t>(std::numeric_limits<int>::max());

            if (validCache)
            {
                const size_t compressedSize = static_cast<size_t>(header.compressedSize);
                const size_t originalSize = static_cast<size_t>(header.originalSize);

                std::vector<uint8_t> compressedData(compressedSize);
                cacheFile.read(reinterpret_cast<char*>(compressedData.data()), compressedSize);
                if (!cacheFile)
                {
                    validCache = false;
                }
                else
                {
                    std::vector<uint8_t> uncompressedData(originalSize);
                    const size_t decompressedSize = lzav_decompress(
                        compressedData.data(), uncompressedData.data(),
                        static_cast<int>(compressedSize), static_cast<int>(originalSize));

                    if (decompressedSize != originalSize
                        || HashBuffer(uncompressedData.data(), uncompressedData.size()) != header.dataHash)
                    {
                        validCache = false;
                    }
                    else
                    {
                        size_t offset = 0;
                        auto readFromBuffer = [&](void* dst, size_t readSize) -> bool
                        {
                            if (offset + readSize > uncompressedData.size())
                            {
                                return false;
                            }
                            std::memcpy(dst, uncompressedData.data() + offset, readSize);
                            offset += readSize;
                            return true;
                        };

                        size_t mipCount = 0;
                        if (!readFromBuffer(&payload.Width, sizeof(int))
                            || !readFromBuffer(&payload.Height, sizeof(int))
                            || !readFromBuffer(&payload.MipLevels, sizeof(uint32_t))
                            || !readFromBuffer(&payload.SH, sizeof(Assets::SphericalHarmonics))
                            || !readFromBuffer(&mipCount, sizeof(size_t))
                            || payload.Width <= 0
                            || payload.Height <= 0
                            || mipCount == 0)
                        {
                            validCache = false;
                        }
                        else
                        {
                            payload.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
                            payload.MipDimensions.resize(mipCount);
                            for (auto& dim : payload.MipDimensions)
                            {
                                if (!readFromBuffer(&dim.first, sizeof(int))
                                    || !readFromBuffer(&dim.second, sizeof(int))
                                    || dim.first <= 0
                                    || dim.second <= 0)
                                {
                                    validCache = false;
                                    break;
                                }
                            }
                        }

                        if (validCache)
                        {
                            const size_t baseFloats = static_cast<size_t>(payload.Width) * payload.Height * 4;
                            payload.BasePixels.resize(baseFloats);
                            if (!readFromBuffer(payload.BasePixels.data(), baseFloats * sizeof(float)))
                            {
                                validCache = false;
                            }
                        }

                        if (validCache)
                        {
                            payload.MipLevelData.clear();
                            payload.MipLevelData.resize(mipCount);
                            for (auto& mipData : payload.MipLevelData)
                            {
                                size_t mipSize = 0;
                                if (!readFromBuffer(&mipSize, sizeof(size_t)))
                                {
                                    validCache = false;
                                    break;
                                }
                                mipData.resize(mipSize);
                                if (mipSize > 0 && !readFromBuffer(mipData.data(), mipSize * sizeof(float)))
                                {
                                    validCache = false;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            cacheFile.close();
        }

        if (!validCache)
        {
            std::error_code removeError;
            std::filesystem::remove(cacheFilePath, removeError);
        }
        return validCache;
    }

    void SaveHDRTexturePayloadToCache(const std::string& textureName, const Assets::FHDRTexturePayload& payload)
    {
        std::vector<uint8_t> uncompressedData;
        auto writeToBuffer = [&](const void* src, size_t writeSize)
        {
            const uint8_t* bytes = static_cast<const uint8_t*>(src);
            uncompressedData.insert(uncompressedData.end(), bytes, bytes + writeSize);
        };

        writeToBuffer(&payload.Width, sizeof(int));
        writeToBuffer(&payload.Height, sizeof(int));
        writeToBuffer(&payload.MipLevels, sizeof(uint32_t));
        writeToBuffer(&payload.SH, sizeof(Assets::SphericalHarmonics));

        const size_t mipCount = payload.MipDimensions.size();
        writeToBuffer(&mipCount, sizeof(size_t));
        for (const auto& dim : payload.MipDimensions)
        {
            writeToBuffer(&dim.first, sizeof(int));
            writeToBuffer(&dim.second, sizeof(int));
        }

        writeToBuffer(payload.BasePixels.data(), payload.BasePixels.size() * sizeof(float));

        for (const auto& mipData : payload.MipLevelData)
        {
            const size_t mipSize = mipData.size();
            writeToBuffer(&mipSize, sizeof(size_t));
            writeToBuffer(mipData.data(), mipSize * sizeof(float));
        }

        const size_t uncompressedSize = uncompressedData.size();
        if (uncompressedSize > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            return;
        }

        const size_t compressedBound = lzav_compress_bound_hi(static_cast<int>(uncompressedSize));
        std::vector<uint8_t> compressedData(compressedBound);
        const size_t actualCompressedSize = lzav_compress_hi(
            uncompressedData.data(), compressedData.data(),
            static_cast<int>(uncompressedSize), static_cast<int>(compressedBound));
        if (actualCompressedSize == 0)
        {
            return;
        }

        HdrCacheHeader header{};
        header.magic = kHdrCacheMagic;
        header.version = kHdrCacheVersion;
        header.originalSize = static_cast<uint64_t>(uncompressedSize);
        header.compressedSize = static_cast<uint64_t>(actualCompressedSize);
        header.dataHash = HashBuffer(uncompressedData.data(), uncompressedData.size());

        const std::string cacheFileName = Assets::HDRCacheFileName(textureName);
        std::filesystem::path cacheFilePath(cacheFileName);
        std::filesystem::path tempCachePath = cacheFilePath;
        tempCachePath += ".tmp";

        std::ofstream cacheFile(tempCachePath, std::ios::binary | std::ios::trunc);
        if (!cacheFile.is_open())
        {
            std::error_code removeTempError;
            std::filesystem::remove(tempCachePath, removeTempError);
            return;
        }

        cacheFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        cacheFile.write(reinterpret_cast<const char*>(compressedData.data()), actualCompressedSize);
        cacheFile.flush();
        cacheFile.close();

        std::error_code removeError;
        std::filesystem::remove(cacheFilePath, removeError);

        std::error_code renameError;
        std::filesystem::rename(tempCachePath, cacheFilePath, renameError);
        if (renameError)
        {
            std::filesystem::remove(tempCachePath);
        }
    }

    SDL_Window* startupSplashWindow = nullptr;
    uint32_t startupSplashStage = 0;

    void BlendStartupLightPixel(SDL_Surface* surface, int x, int y, float coverage)
    {
        Uint8 red = 0, green = 0, blue = 0, alpha = 0;
        if (coverage <= 0 || !SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha)) return;
        auto blend = [coverage](Uint8 channel) { return static_cast<Uint8>(std::lround(channel + (255 - channel) * coverage)); };
        SDL_WriteSurfacePixel(surface, x, y, blend(red), blend(green), blend(blue), alpha);
    }

    void DrawStartupLight(SDL_Surface* surface, int centerX, int centerY, int radius, bool filled)
    {
        constexpr int grid = 4, samples = grid * grid;
        const float outer = radius + .5f, inner = radius - 2.5f, outer2 = outer * outer, inner2 = inner * inner;
        for (int y = -radius - 1; y <= radius + 1; ++y)
        {
            for (int x = -radius - 1; x <= radius + 1; ++x)
            {
                int covered = 0;
                for (int i = 0; i < samples; ++i)
                {
                    const float sampleX = x + (i % grid + .5f) / grid;
                    const float sampleY = y + (i / grid + .5f) / grid;
                    const float distance2 = sampleX * sampleX + sampleY * sampleY;
                    covered += distance2 <= outer2 && (filled || distance2 >= inner2);
                }
                BlendStartupLightPixel(surface, centerX + x, centerY + y, static_cast<float>(covered) / samples);
            }
        }
    }

    void DrawStartupLights(SDL_Surface* surface, uint32_t completedStages)
    {
        constexpr int count = 3, radius = 8, gap = 18;
        const int width = count * radius * 2 + (count - 1) * gap;
        const int firstX = (surface->w - width) / 2 + radius;
        const int centerY = surface->h - 42;
        const SDL_Rect lightArea{firstX - radius - 1, centerY - radius - 1, width + 2, radius * 2 + 2};
        SDL_FillSurfaceRect(surface, &lightArea, SDL_MapSurfaceRGB(surface, 19, 22, 29));
        for (int i = 0; i < count; ++i)
        {
            DrawStartupLight(surface, firstX + i * (radius * 2 + gap), centerY, radius, i < completedStages);
        }
    }

    void CloseStartupSplashImpl()
    {
        if (startupSplashWindow != nullptr)
        {
            SDL_DestroyWindow(startupSplashWindow);
            startupSplashWindow = nullptr;
        }
        startupSplashStage = 0;
    }

    std::filesystem::path ResolveApplicationIconPath(const std::string& explicitIconPath)
    {
        if (!explicitIconPath.empty())
        {
            const std::filesystem::path explicitPath = Utilities::FileHelper::GetRuntimeFilePath(explicitIconPath);
            if (std::filesystem::exists(explicitPath))
            {
                return explicitPath;
            }
        }

        const std::string appIdentity = NextRenderer::GetApplicationIdentity();
        if (!appIdentity.empty())
        {
            const std::filesystem::path appIconPath = Utilities::FileHelper::GetRuntimeFilePath(
                "assets/icons/" + appIdentity + ".png");
            if (std::filesystem::exists(appIconPath))
            {
                return appIconPath;
            }

            const std::filesystem::path manifestPath = Utilities::FileHelper::GetRuntimeFilePath(
                "assets/icons/manifest.json");
            if (std::filesystem::exists(manifestPath))
            {
                try
                {
                    std::ifstream manifestFile(manifestPath);
                    if (manifestFile.is_open())
                    {
                        nlohmann::json manifestJson;
                        manifestFile >> manifestJson;
                        if (manifestJson.contains("icons") && manifestJson["icons"].is_array())
                        {
                            for (const auto& item : manifestJson["icons"])
                            {
                                if (item.value("target", "") == appIdentity)
                                {
                                    const std::string fileStr = item.value("file", "");
                                    if (!fileStr.empty())
                                    {
                                        const std::filesystem::path resolved =
                                            Utilities::FileHelper::GetRuntimeFilePath("assets/icons/" + fileStr);
                                        if (std::filesystem::exists(resolved))
                                        {
                                            return resolved;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                catch (...)
                {
                }
            }
        }

        const std::filesystem::path fallbackEnginePath = Utilities::FileHelper::GetRuntimeFilePath(
            "assets/icons/gkNextEngine.png");
        if (std::filesystem::exists(fallbackEnginePath))
        {
            return fallbackEnginePath;
        }

        const std::filesystem::path fallbackRendererPath = Utilities::FileHelper::GetRuntimeFilePath(
            "assets/icons/gkNextRenderer.png");
        if (std::filesystem::exists(fallbackRendererPath))
        {
            return fallbackRendererPath;
        }

        return Utilities::FileHelper::GetRuntimeFilePath("assets/brand/gknext_logo_icon_200.png");
    }

    void ApplyWindowIconImpl(SDL_Window* window, const std::string& explicitIconPath)
    {
        if (window == nullptr)
        {
            return;
        }

        const std::filesystem::path iconPath = ResolveApplicationIconPath(explicitIconPath);
        if (!std::filesystem::exists(iconPath))
        {
            return;
        }

        int iconWidth = 0;
        int iconHeight = 0;
        if (stbi_uc* pixels = stbi_load(iconPath.string().c_str(), &iconWidth, &iconHeight, nullptr, STBI_rgb_alpha);
            pixels != nullptr)
        {
            if (SDL_Surface* surface = SDL_CreateSurfaceFrom(
                    iconWidth, iconHeight, SDL_PIXELFORMAT_RGBA32, pixels, iconWidth * 4);
                surface != nullptr)
            {
                if (!SDL_SetWindowIcon(window, surface))
                {
                    SPDLOG_WARN("Failed to set window icon: {}", SDL_GetError());
                }
                else
                {
                    SPDLOG_INFO("Set window icon for {} from {}",
                                NextRenderer::GetApplicationIdentity(), iconPath.string());
                }
                SDL_DestroySurface(surface);
            }
            stbi_image_free(pixels);
        }
    }

    void CreateStartupSplashImpl()
    {
#if ANDROID || IOS
        return;
#else
        constexpr int splashWidth = 480;
        constexpr int splashHeight = 270;
        const SDL_WindowFlags flags = SDL_WINDOW_HIDDEN |
                                      SDL_WINDOW_BORDERLESS |
                                      SDL_WINDOW_ALWAYS_ON_TOP |
                                      SDL_WINDOW_UTILITY |
                                      SDL_WINDOW_NOT_FOCUSABLE;

        startupSplashWindow = SDL_CreateWindow("gkNext", splashWidth, splashHeight, flags);
        if (startupSplashWindow == nullptr)
        {
            SPDLOG_WARN("Failed to create startup splash window: {}", SDL_GetError());
            return;
        }

        SDL_SetWindowPosition(startupSplashWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_Surface* windowSurface = SDL_GetWindowSurface(startupSplashWindow);
        if (windowSurface == nullptr)
        {
            SPDLOG_WARN("Failed to create startup splash surface: {}", SDL_GetError());
            SDL_ShowWindow(startupSplashWindow);
            return;
        }

        SDL_FillSurfaceRect(windowSurface, nullptr, SDL_MapSurfaceRGB(windowSurface, 19, 22, 29));

        const std::filesystem::path logoPath = ResolveApplicationIconPath("");
        int logoWidth = 0;
        int logoHeight = 0;
        if (std::filesystem::exists(logoPath))
        {
            if (stbi_uc* pixels = stbi_load(logoPath.string().c_str(), &logoWidth, &logoHeight, nullptr, STBI_rgb_alpha);
                pixels != nullptr)
            {
                SDL_Surface* surface = SDL_CreateSurfaceFrom(
                    logoWidth, logoHeight, SDL_PIXELFORMAT_RGBA32, pixels, logoWidth * 4);
                if (surface != nullptr)
                {
                    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
                    const int iconSize = static_cast<int>(std::min(windowSurface->w, windowSurface->h) * 0.42f);
                    const SDL_Rect destination{
                        (windowSurface->w - iconSize) / 2,
                        (windowSurface->h - iconSize) / 2,
                        iconSize,
                        iconSize};
                    SDL_BlitSurfaceScaled(surface, nullptr, windowSurface, &destination, SDL_SCALEMODE_LINEAR);
                    SDL_DestroySurface(surface);
                }
                stbi_image_free(pixels);
            }
        }

        DrawStartupLights(windowSurface, startupSplashStage);
        SDL_ShowWindow(startupSplashWindow);
        SDL_UpdateWindowSurface(startupSplashWindow);
#endif
    }

    void AdvanceStartupSplashStageImpl()
    {
        if (startupSplashWindow == nullptr || startupSplashStage >= 3)
        {
            return;
        }

        SDL_Surface* windowSurface = SDL_GetWindowSurface(startupSplashWindow);
        if (windowSurface == nullptr)
        {
            return;
        }

        ++startupSplashStage;
        DrawStartupLights(windowSurface, startupSplashStage);
        SDL_UpdateWindowSurface(startupSplashWindow);
    }

    class FTextureDecodeProvider final : public Assets::ITextureDecodeProvider
    {
    public:
        bool DecodeLdrTexture(const std::string& texname,
                              const std::string& mime,
                              const uint8_t* data,
                              size_t byteLength,
                              bool srgb,
                              bool supportsBCTextures,
                              Assets::FDecodedLdrTexture& outDecoded) override
        {
            int width = 0;
            int height = 0;
            int channels = 4;
            uint8_t* stbdata = nullptr;
            uint8_t* pixels = nullptr;
            uint32_t size = 0;
            VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
            ktxTexture2* kTexture = nullptr;
            ktx_error_code_e result = KTX_SUCCESS;
            bool useWebPFree = false;

            if (mime.find("image/ktx") != std::string::npos)
            {
                auto loadKtxFromMemory = [&]() -> bool
                {
                    result = ktxTexture2_CreateFromMemory(data, byteLength, KTX_TEXTURE_CREATE_CHECK_GLTF_BASISU_BIT, &kTexture);
                    if (KTX_SUCCESS != result) return false;
                    result = ktxTexture2_TranscodeBasis(
                        kTexture, supportsBCTextures ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32, 0);
                    if (KTX_SUCCESS != result) return false;
                    pixels = ktxTexture_GetData(ktxTexture(kTexture));

                    ktx_size_t offset = 0;
                    ktxTexture_GetImageOffset(ktxTexture(kTexture), 0, 0, 0, &offset);
                    pixels += offset;
                    size = static_cast<uint32_t>(ktxTexture_GetImageSize(ktxTexture(kTexture), 0));

                    format = static_cast<VkFormat>(kTexture->vkFormat);
                    width = static_cast<int>(kTexture->baseWidth);
                    height = static_cast<int>(kTexture->baseHeight);
                    return true;
                };

                if (!loadKtxFromMemory())
                {
                    SPDLOG_ERROR("load texture {} failed.", texname);
                }
            }
            else if (mime.find("image/webp") != std::string::npos)
            {
                stbdata = WebPDecodeRGBA(data, byteLength, &width, &height);
                if (stbdata)
                {
                    size = static_cast<uint32_t>(width * height * 4);
                    pixels = stbdata;
                    format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                    useWebPFree = true;
                }
                else
                {
                    SPDLOG_ERROR("Failed to decode WebP image: {}", texname);
                }
            }
            else
            {
                std::hash<std::string> hasher;
                std::string cacheFileName = Utilities::CookHelper::GetCookedFileName(
                    fmt::format("{:016x}", hasher(texname)), "texktx");
#if ANDROID
                constexpr bool useKtxDiskCache = false;
#else
                constexpr bool useKtxDiskCache = true;
#endif
                if (!useKtxDiskCache || !std::filesystem::exists(cacheFileName))
                {
                    stbdata = stbi_load_from_memory(
                        data, static_cast<int>(byteLength), &width, &height, &channels, STBI_rgb_alpha);
                    if (stbdata != nullptr && width > 0 && height > 0)
                    {
                        format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                        size = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4u;

                        ktxTextureCreateInfo createInfo = {
                            0,
                            static_cast<uint32_t>(format),
                            0,
                            static_cast<uint32_t>(width),
                            static_cast<uint32_t>(height),
                            1, 2, 1, 1, 1, KTX_FALSE, KTX_FALSE
                        };

                        result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &kTexture);
                        if (result != KTX_SUCCESS) Throw(std::runtime_error("failed to create ktx2 image "));

                        std::memcpy(ktxTexture_GetData(ktxTexture(kTexture)), stbdata, size);

                        ktxBasisParams params = {};
                        params.structSize = sizeof(params);
                        params.uastc = KTX_TRUE;
                        params.compressionLevel = 2;
                        params.qualityLevel = 128;
                        params.threadCount = 12;
                        result = ktxTexture2_CompressBasisEx(kTexture, &params);
                        if (KTX_SUCCESS != result) Throw(std::runtime_error("failed to compress ktx2 image "));

                        if (useKtxDiskCache)
                        {
                            result = ktxTexture_WriteToNamedFile(ktxTexture(kTexture), cacheFileName.c_str());
                            if (result != KTX_SUCCESS)
                            {
                                SPDLOG_WARN("Failed to cache KTX2 texture '{}': {}", texname, static_cast<int>(result));
                            }
                        }
                    }
                    else
                    {
                        SPDLOG_ERROR("Failed to decode image: {}", texname);
                    }
                }
                else
                {
                    result = ktxTexture2_CreateFromNamedFile(
                        cacheFileName.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);
                    if (result != KTX_SUCCESS) Throw(std::runtime_error("failed to load ktx2 image "));
                }

                if (kTexture != nullptr)
                {
                    result = ktxTexture2_TranscodeBasis(
                        kTexture, supportsBCTextures ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32, 0);
                    if (result != KTX_SUCCESS) Throw(std::runtime_error("failed to transcode ktx2 image "));

                    pixels = ktxTexture_GetData(ktxTexture(kTexture));
                    ktx_size_t offset = 0;
                    ktxTexture_GetImageOffset(ktxTexture(kTexture), 0, 0, 0, &offset);
                    pixels += offset;
                    size = static_cast<uint32_t>(ktxTexture_GetImageSize(ktxTexture(kTexture), 0));

                    format = supportsBCTextures
                        ? (srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK)
                        : (srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM);
                    width = static_cast<int>(kTexture->baseWidth);
                    height = static_cast<int>(kTexture->baseHeight);
                }
            }

            const bool ok = pixels != nullptr && size > 0 && width > 0 && height > 0;
            if (ok)
            {
                outDecoded.Pixels.assign(pixels, pixels + size);
                outDecoded.Width = width;
                outDecoded.Height = height;
                outDecoded.MipLevels = 1;
                outDecoded.Format = format;
            }

            if (stbdata != nullptr)
            {
                if (useWebPFree) WebPFree(stbdata);
                else stbi_image_free(stbdata);
            }
            if (kTexture != nullptr)
            {
                ktxTexture_Destroy(ktxTexture(kTexture));
            }
            return ok;
        }

        Assets::FHDRTexturePayload LoadHdrTexture(const std::string& texname,
                                                  const uint8_t* data,
                                                  size_t byteLength) override
        {
            Assets::FHDRTexturePayload payload{};
            if (LoadHDRTexturePayloadFromCache(texname, payload))
            {
                return payload;
            }

            if (data == nullptr || byteLength == 0)
            {
                return payload;
            }

            int channels = 4;
            float* pixels = stbi_loadf_from_memory(
                data, static_cast<int>(byteLength), &payload.Width, &payload.Height, &channels, STBI_rgb_alpha);
            if (pixels == nullptr)
            {
                return payload;
            }

            payload.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
            const size_t baseFloats = static_cast<size_t>(payload.Width) * static_cast<size_t>(payload.Height) * 4u;
            payload.BasePixels.assign(pixels, pixels + baseFloats);
            payload.SH = Assets::ProjectHdrToSh(payload.BasePixels.data(), payload.Width, payload.Height);
            PrefilterHdrEnvironmentMap(payload.BasePixels.data(), payload.Width, payload.Height,
                                       payload.MipLevelData, payload.MipDimensions);
            payload.MipLevels = static_cast<uint32_t>(payload.MipLevelData.size());
            stbi_image_free(pixels);

            SaveHDRTexturePayloadToCache(texname, payload);
            return payload;
        }
    };
}

namespace Modules::TextureCodecs
{
    void Install()
    {
        static FTextureDecodeProvider provider;
        Assets::GlobalTexturePool::SetDecodeProvider(&provider);
        Vulkan::Window::SetBrandingHooks({
            .ApplyWindowIcon = &ApplyWindowIconImpl,
            .CreateStartupSplash = &CreateStartupSplashImpl,
            .AdvanceStartupSplashStage = &AdvanceStartupSplashStageImpl,
            .CloseStartupSplash = &CloseStartupSplashImpl,
        });
    }
}
