#pragma once

// ============================================================================
// NetBuffer.hpp - Byte writer/reader for network messages. Little-endian host
// order is assumed on every supported platform (x86_64, arm64), so trivially
// copyable values are copied as-is. Strings carry a u16 length. The reader
// never reads past the end: an underflow sets Failed() and yields zeros, so a
// malformed packet is dropped by the caller instead of crashing it.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NextNet
{
    class FNetWriter
    {
    public:
        template <class T>
        FNetWriter& Write(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>, "network values must be trivially copyable");
            const size_t offset = bytes_.size();
            bytes_.resize(offset + sizeof(T));
            std::memcpy(bytes_.data() + offset, &value, sizeof(T));
            return *this;
        }

        FNetWriter& WriteString(std::string_view text)
        {
            const auto length = static_cast<uint16_t>(std::min<size_t>(text.size(), 0xFFFF));
            Write(length);
            const size_t offset = bytes_.size();
            bytes_.resize(offset + length);
            std::memcpy(bytes_.data() + offset, text.data(), length);
            return *this;
        }

        std::span<const std::byte> Bytes() const { return bytes_; }
        size_t Size() const { return bytes_.size(); }
        void Clear() { bytes_.clear(); }

    private:
        std::vector<std::byte> bytes_;
    };

    class FNetReader
    {
    public:
        explicit FNetReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

        template <class T>
        T Read()
        {
            static_assert(std::is_trivially_copyable_v<T>, "network values must be trivially copyable");
            T value{};
            if (!Take(sizeof(T)))
            {
                return value;
            }
            std::memcpy(&value, bytes_.data() + offset_ - sizeof(T), sizeof(T));
            return value;
        }

        template <class T>
        bool Read(T& out)
        {
            out = Read<T>();
            return !failed_;
        }

        std::string ReadString()
        {
            const auto length = Read<uint16_t>();
            if (!Take(length))
            {
                return {};
            }
            return std::string(reinterpret_cast<const char*>(bytes_.data() + offset_ - length), length);
        }

        bool Failed() const { return failed_; }
        bool AtEnd() const { return offset_ >= bytes_.size(); }
        size_t Remaining() const { return failed_ ? 0 : bytes_.size() - offset_; }

    private:
        bool Take(size_t count)
        {
            if (failed_ || bytes_.size() - offset_ < count)
            {
                failed_ = true;
                return false;
            }
            offset_ += count;
            return true;
        }

        std::span<const std::byte> bytes_;
        size_t offset_ = 0;
        bool failed_ = false;
    };
}
