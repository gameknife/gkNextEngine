// FAmbientBakeCache: key derivation and file IO for the ambient GI bake disk cache.
// See AmbientBakeCache.hpp for the contract.
#include "Engine/Assets/Acceleration/AmbientBakeCache.hpp"

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <xxhash.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <type_traits>

namespace Assets::CPU
{

namespace
{
    constexpr uint32_t kCacheMagic = 0x42414B47u; // 'GKAB'
    // Bump when the bake itself changes meaning: bake shaders, the distance-field transform, the
    // convergence pass count, or the brick classification. Struct sizes and grid constants are
    // hashed directly below and need no bump.
    constexpr uint32_t kCacheVersion = 1u;

    // Streaming XXH64 so a multi-million-vertex scene never needs a staging byte buffer.
    class FHashStream
    {
    public:
        FHashStream()
        {
            state_ = XXH64_createState();
            if (state_ != nullptr)
            {
                XXH64_reset(state_, 0);
            }
        }

        ~FHashStream()
        {
            if (state_ != nullptr)
            {
                XXH64_freeState(state_);
            }
        }

        FHashStream(const FHashStream&) = delete;
        FHashStream& operator=(const FHashStream&) = delete;

        bool Valid() const { return state_ != nullptr; }

        void AddBytes(const void* data, size_t size)
        {
            if (state_ != nullptr && data != nullptr && size > 0)
            {
                XXH64_update(state_, data, size);
            }
        }

        template <typename T>
        void Add(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>, "hash input must be trivially copyable");
            AddBytes(&value, sizeof(T));
        }

        uint64_t Digest() const { return state_ != nullptr ? XXH64_digest(state_) : 0u; }

    private:
        XXH64_state_t* state_ = nullptr;
    };

    template <typename T>
    bool WritePod(std::ofstream& file, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "pod write only");
        file.write(reinterpret_cast<const char*>(&value), sizeof(T));
        return file.good();
    }

    template <typename T>
    bool ReadPod(std::ifstream& file, T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "pod read only");
        file.read(reinterpret_cast<char*>(&value), sizeof(T));
        return file.good();
    }

    // Section layout: rawSize (u64), compressedSize (u64), compressed bytes.
    template <typename T>
    bool WriteSection(std::ofstream& file, const std::vector<T>& data)
    {
        const uint64_t rawSize = static_cast<uint64_t>(data.size()) * sizeof(T);
        if (rawSize == 0)
        {
            return WritePod(file, rawSize) && WritePod(file, uint64_t{0});
        }
        if (rawSize > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
        {
            return false;
        }

        const int bound = lzav_compress_bound(static_cast<int>(rawSize));
        std::vector<uint8_t> compressed(static_cast<size_t>(bound));
        const int compressedSize = lzav_compress_default(data.data(), compressed.data(),
                                                         static_cast<int>(rawSize), bound);
        if (compressedSize <= 0)
        {
            return false;
        }

        if (!WritePod(file, rawSize) || !WritePod(file, static_cast<uint64_t>(compressedSize)))
        {
            return false;
        }
        file.write(reinterpret_cast<const char*>(compressed.data()), compressedSize);
        return file.good();
    }

    template <typename T>
    bool ReadSection(std::ifstream& file, std::vector<T>& out, size_t expectedCount)
    {
        uint64_t rawSize = 0;
        uint64_t compressedSize = 0;
        if (!ReadPod(file, rawSize) || !ReadPod(file, compressedSize))
        {
            return false;
        }
        if (rawSize != static_cast<uint64_t>(expectedCount) * sizeof(T))
        {
            return false;
        }
        if (rawSize == 0)
        {
            out.clear();
            return compressedSize == 0;
        }
        if (compressedSize == 0 || compressedSize > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) ||
            rawSize > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
        {
            return false;
        }

        std::vector<uint8_t> compressed(static_cast<size_t>(compressedSize));
        file.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressedSize));
        if (!file.good())
        {
            return false;
        }

        out.resize(expectedCount);
        const int decompressed = lzav_decompress(compressed.data(), out.data(),
                                                 static_cast<int>(compressedSize), static_cast<int>(rawSize));
        return decompressed == static_cast<int>(rawSize);
    }

    void HashModelGeometry(FHashStream& stream, const Assets::Model& model, std::vector<glm::vec3>& scratch)
    {
        const std::vector<Assets::Vertex>& vertices = model.CPUVertices();
        const std::vector<uint32_t>& indices = model.CPUIndices();
        stream.Add(static_cast<uint64_t>(vertices.size()));
        stream.Add(static_cast<uint64_t>(indices.size()));
        stream.AddBytes(indices.data(), indices.size() * sizeof(uint32_t));

        // Vertex is 16-byte aligned and carries tail padding, so hashing the array raw would feed
        // uninitialized bytes into the key. Only the positions matter to voxelization anyway.
        scratch.clear();
        scratch.reserve(vertices.size());
        for (const Assets::Vertex& vertex : vertices)
        {
            scratch.push_back(glm::vec3(vertex.Position));
        }
        stream.AddBytes(scratch.data(), scratch.size() * sizeof(glm::vec3));
    }
}

bool IsAmbientBakeDiskCacheEnabled()
{
    NextEngine* engine = NextEngine::GetInstance();
    return engine == nullptr || engine->GetUserSettings().AmbientCubeDiskCache;
}

uint64_t ComputeAmbientBakeKey(const Assets::Scene& scene, const FAmbientBakeKeyInputs& inputs)
{
    FHashStream stream;
    if (!stream.Valid())
    {
        return 0u;
    }

    // Format and layout. A changed struct size or grid constant invalidates every cache entry
    // without anyone having to remember the version number.
    stream.Add(kCacheVersion);
    stream.Add(static_cast<uint32_t>(sizeof(Assets::VoxelData)));
    stream.Add(static_cast<uint32_t>(sizeof(Assets::AmbientCube)));
    stream.Add(static_cast<uint32_t>(sizeof(Assets::PageIndex)));
    stream.Add(static_cast<uint32_t>(Assets::CUBE_SIZE_XY));
    stream.Add(static_cast<uint32_t>(Assets::CUBE_SIZE_Z));
    stream.Add(static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_EDGE));
    stream.Add(static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE));
    stream.Add(static_cast<uint32_t>(Assets::ACGI_PAGE_COUNT));

    // Grid, pool and bake backend.
    stream.Add(inputs.baseUnit);
    stream.Add(inputs.offsetBias);
    stream.Add(inputs.cascadeCount);
    stream.Add(inputs.cascadeRatio);
    stream.Add(inputs.poolBricksPerCascade);
    stream.Add(static_cast<uint32_t>(inputs.hardwareBake ? 1u : 0u));

    // GI-participating instances. This filter must stay identical to the CPU TLAS capture in
    // FCPUAccelerationStructure::CaptureBuildInput, or the key would cover a different set of
    // geometry than the bake actually traced.
    uint32_t instanceCount = 0;
    for (const auto* render : scene.Components<Runtime::RenderComponent>())
    {
        const Assets::Node* node = render->GetOwner();
        if (node == nullptr)
        {
            continue;
        }
        const uint32_t modelId = render->GetModelId();
        if (modelId == static_cast<uint32_t>(-1) || !render->GetVisible())
        {
            continue;
        }
        const uint32_t participation = render->GetRenderParticipationMask();
        if ((participation & (Runtime::RenderParticipation::giBake | Runtime::RenderParticipation::gpuAs)) == 0u)
        {
            continue;
        }

        ++instanceCount;
        stream.Add(modelId);
        const glm::mat4 worldTransform = node->WorldTransform();
        stream.AddBytes(&worldTransform[0][0], sizeof(glm::mat4));
        const std::array<uint32_t, 16>& materials = render->GetMaterials();
        stream.AddBytes(materials.data(), materials.size() * sizeof(uint32_t));
    }
    stream.Add(instanceCount);

    // Model geometry.
    std::vector<glm::vec3> scratch;
    stream.Add(static_cast<uint64_t>(scene.Models().size()));
    for (const Assets::Model& model : scene.Models())
    {
        HashModelGeometry(stream, model, scratch);
    }

    // Materials drive bounce colour and emission; Material is a packed GPU struct with no padding.
    stream.Add(static_cast<uint64_t>(scene.Materials().size()));
    for (const Assets::FMaterial& material : scene.Materials())
    {
        stream.AddBytes(&material.gpuMaterial_, sizeof(Assets::Material));
    }

    // Direct lighting.
    stream.Add(static_cast<uint64_t>(scene.Lights().size()));
    stream.AddBytes(scene.Lights().data(), scene.Lights().size() * sizeof(Assets::LightObject));

    // Sun and sky.
    const Assets::EnvironmentSetting& env = scene.GetEnvSettings();
    stream.Add(static_cast<uint32_t>(env.HasSun ? 1u : 0u));
    stream.Add(static_cast<uint32_t>(env.HasSky ? 1u : 0u));
    stream.Add(env.SunRotation);
    stream.Add(env.SunElevation);
    stream.Add(env.SunIntensity);
    stream.Add(env.SunColor);
    stream.Add(env.SkyIdx);
    stream.Add(env.SkyIntensity);
    stream.Add(env.SkyColor);
    stream.Add(env.SkyRotation);

    return stream.Digest();
}

std::string GetAmbientBakeCachePath(uint64_t key)
{
    return Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", key), "ambient");
}

bool LoadAmbientBakeCache(uint64_t key, FAmbientBakeCachePayload& outPayload)
{
    const std::string path = GetAmbientBakeCachePath(key);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    uint64_t storedKey = 0;
    uint32_t cascadeCount = 0;
    uint32_t cascadeCapacity = 0;
    uint32_t voxelCountPerCascade = 0;
    uint32_t poolBricksPerCascade = 0;
    if (!ReadPod(file, magic) || !ReadPod(file, version) || !ReadPod(file, storedKey) ||
        !ReadPod(file, cascadeCount) || !ReadPod(file, cascadeCapacity) ||
        !ReadPod(file, voxelCountPerCascade) || !ReadPod(file, poolBricksPerCascade))
    {
        return false;
    }

    if (magic != kCacheMagic || version != kCacheVersion || storedKey != key || cascadeCount == 0u ||
        cascadeCapacity < cascadeCount || poolBricksPerCascade == 0u || voxelCountPerCascade == 0u ||
        cascadeCapacity > static_cast<uint32_t>(Assets::CUBE_CASCADE_MAX) ||
        poolBricksPerCascade > static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE))
    {
        return false;
    }

    const size_t brickTableCount =
        static_cast<size_t>(cascadeCapacity) * Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE;
    const size_t activeBrickListCount = static_cast<size_t>(cascadeCapacity) * poolBricksPerCascade;
    const size_t pageCount = static_cast<size_t>(Assets::ACGI_PAGE_COUNT) * Assets::ACGI_PAGE_COUNT;
    const size_t cubeCount = activeBrickListCount * Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME;

    FAmbientBakeCachePayload payload;
    payload.cascadeCount = cascadeCount;
    payload.cascadeCapacity = cascadeCapacity;
    payload.voxelCountPerCascade = voxelCountPerCascade;
    payload.poolBricksPerCascade = poolBricksPerCascade;
    if (!ReadSection(file, payload.voxels, static_cast<size_t>(cascadeCount) * voxelCountPerCascade) ||
        !ReadSection(file, payload.brickTable, brickTableCount) ||
        !ReadSection(file, payload.activeBrickList, activeBrickListCount) ||
        !ReadSection(file, payload.activeBricksPerCascade, cascadeCapacity) ||
        !ReadSection(file, payload.pages, pageCount) ||
        !ReadSection(file, payload.cubes, cubeCount))
    {
        return false;
    }

    outPayload = std::move(payload);
    return true;
}

bool SaveAmbientBakeCache(uint64_t key, const FAmbientBakeCachePayload& payload)
{
    const std::string path = GetAmbientBakeCachePath(key);
    const std::string tempPath = path + ".tmp";

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            return false;
        }

        const bool headerOk =
            WritePod(file, kCacheMagic) && WritePod(file, kCacheVersion) && WritePod(file, key) &&
            WritePod(file, payload.cascadeCount) && WritePod(file, payload.cascadeCapacity) &&
            WritePod(file, payload.voxelCountPerCascade) && WritePod(file, payload.poolBricksPerCascade);
        if (!headerOk || !WriteSection(file, payload.voxels) || !WriteSection(file, payload.brickTable) ||
            !WriteSection(file, payload.activeBrickList) ||
            !WriteSection(file, payload.activeBricksPerCascade) || !WriteSection(file, payload.pages) ||
            !WriteSection(file, payload.cubes))
        {
            file.close();
            std::error_code ignored;
            std::filesystem::remove(tempPath, ignored);
            return false;
        }
    }

    // Rename last so a crash mid-write can never leave a truncated file under the real key.
    std::error_code errorCode;
    std::filesystem::rename(tempPath, path, errorCode);
    if (errorCode)
    {
        std::filesystem::remove(tempPath, errorCode);
        return false;
    }
    return true;
}

}
