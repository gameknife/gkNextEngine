#pragma once

#include <bit>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

namespace NextDayzBake
{
    using Json = nlohmann::json;
    namespace fs = std::filesystem;

    inline auto Output(const fs::path& path)
    {
        std::ofstream stream;
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(path, std::ios::binary);
        return stream;
    }

    inline void WriteJson(const fs::path& path, const Json& data)
    {
        auto stream = Output(path);
        stream << data.dump(2) << '\n';
        stream.close();
    }

    inline std::vector<uint8_t> ReadBytes(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Cannot read " + path.string());
        }
        return {std::istreambuf_iterator<char>(stream), {}};
    }

    template<glm::length_t count, class T, glm::qualifier Q>
    std::array<T, count> Array(const glm::vec<count, T, Q>& value)
    {
        std::array<T, count> result{};
        for (glm::length_t index = 0; index < count; ++index)
        {
            result[index] = value[index];
        }
        return result;
    }

    template<class T, glm::qualifier Q>
    std::array<T, 16> Array(const glm::mat<4, 4, T, Q>& value)
    {
        std::array<T, 16> result{};
        for (size_t column = 0; column < 4; ++column)
        {
            const auto values = Array(value[column]);
            std::copy(values.begin(), values.end(), result.begin() + 4 * column);
        }
        return result;
    }

    struct Aabb
    {
        glm::vec3 min{std::numeric_limits<float>::max()};
        glm::vec3 max{-std::numeric_limits<float>::max()};

        void Include(glm::vec3 point)
        {
            min = glm::min(min, point);
            max = glm::max(max, point);
        }

        Json ToJson() const { return {Array(min), Array(max)}; }
    };

    class Binary
    {
    public:
        explicit Binary(const fs::path& path, std::string_view magic) : stream_(Output(path))
        {
            Bytes(magic.data(), magic.size());
        }

        void Bytes(const void* data, size_t length)
        {
            if (length != 0)
            {
                stream_.write(static_cast<const char*>(data), length);
            }
        }

        template<class T>
        void Values(std::span<const T> values)
        {
            static_assert(std::is_arithmetic_v<T> && std::endian::native == std::endian::little);
            Bytes(values.data(), values.size_bytes());
        }

        template<class T>
        void Scalar(T value) { Values(std::span<const T>(&value, 1)); }

        void Count(size_t value)
        {
            if (value > UINT32_MAX) throw std::runtime_error("Binary count overflow");
            Scalar(static_cast<uint32_t>(value));
        }

        template<class T, size_t N>
        void Values(const std::array<T, N>& array) { Values(std::span<const T>(array)); }

        void String(std::string_view value)
        {
            Count(value.size());
            Bytes(value.data(), value.size());
        }

        void Close() { stream_.close(); }

    private:
        std::ofstream stream_;
    };

    void BakeRig(const std::string& source, const fs::path& destination);
    void BakeShared(const fs::path& destination);
    void BakeMap(const std::string& source, const fs::path& destination);
    std::vector<uint8_t> EncodeVertices(const void* data, size_t count, size_t stride);
    std::vector<uint8_t> EncodeTriangles(const std::vector<uint32_t>& indices, size_t vertexCount);
}
