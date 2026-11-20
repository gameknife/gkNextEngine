#pragma once

#include "Engine/Assets/Data/RigDefinition.hpp"

#include <bit>
#include <cmath>
#include <span>
#include <stdexcept>

namespace NextDayz
{
    namespace RigBinary
    {
        class Reader
        {
        public:
            explicit Reader(std::span<const std::byte> data) : data_(data) {}

            std::span<const std::byte> Bytes(size_t count)
            {
                if (count > data_.size()) throw std::runtime_error("Incomplete rig");
                const auto result = data_.first(count);
                data_ = data_.subspan(count);
                return result;
            }

            uint32_t Word()
            {
                const auto bytes = Bytes(4);
                uint32_t result = 0;
                for (int i = 0; i < 4; ++i) result |= std::to_integer<uint32_t>(bytes[i]) << (i * 8);
                return result;
            }

            uint32_t Count(uint32_t maximum)
            {
                const uint32_t count = Word();
                if (count > maximum) throw std::runtime_error("Rig record count exceeds limit");
                return count;
            }

            float Scalar()
            {
                const float value = std::bit_cast<float>(Word());
                if (!std::isfinite(value)) throw std::runtime_error("Non-finite rig value");
                return value;
            }

            std::string String()
            {
                const auto text = Bytes(Count(1024));
                if (text.empty()) throw std::runtime_error("Empty rig name");
                return {reinterpret_cast<const char*>(text.data()), text.size()};
            }

            glm::vec3 Vector()
            {
                const float x = Scalar(), y = Scalar(), z = Scalar();
                return {x, y, z};
            }

            glm::quat Rotation()
            {
                const glm::vec3 xyz = Vector();
                const float w = Scalar();
                return {w, xyz.x, xyz.y, xyz.z};
            }

            template<class T, class Read>
            void Keys(Assets::AnimationChannel<T>& channel, Read value)
            {
                const auto count = Count(65536);
                for (uint32_t i = 0; i < count; ++i)
                {
                    const float time = Scalar();
                    if (!channel.Keys.empty() && time < channel.Keys.back().Time)
                        throw std::runtime_error("Unsorted animation keys");
                    channel.Keys.push_back({time, (this->*value)()});
                }
            }

            bool Done() const { return data_.empty(); }

        private:
            std::span<const std::byte> data_;
        };
    }

    inline Assets::FRigDefinition ReadBakedRig(std::span<const std::byte> bytes)
    {
        RigBinary::Reader reader(bytes);
        if (reader.Word() != 0x3152444e) throw std::runtime_error("Unsupported rig format");
        const auto bones = reader.Count(4096);
        const auto clips = reader.Count(4096);
        if (!bones) throw std::runtime_error("Rig has no skeleton");

        Assets::FRigDefinition rig;
        rig.bones.reserve(bones);
        rig.clips.reserve(clips);
        for (uint32_t i = 0; i < bones; ++i)
        {
            Assets::FRigBone bone;
            bone.name = reader.String();
            bone.parent = std::bit_cast<int32_t>(reader.Word());
            if ((i == 0 && bone.parent != -1) ||
                (i != 0 && (bone.parent < 0 || bone.parent >= static_cast<int32_t>(i))))
                throw std::runtime_error("Invalid rig hierarchy");
            bone.bindT = reader.Vector();
            bone.bindR = reader.Rotation();
            bone.bindS = reader.Vector();
            if (bone.parent >= 0) rig.bones[bone.parent].children.push_back(static_cast<int32_t>(i));
            rig.bones.push_back(std::move(bone));
        }
        for (uint32_t i = 0; i < clips; ++i)
        {
            Assets::FRigClip clip;
            clip.name = reader.String();
            clip.duration = reader.Scalar();
            clip.loop = reader.Bytes(1)[0] != std::byte{0};
            const auto channels = reader.Count(bones);
            clip.channels.reserve(channels);
            for (uint32_t j = 0; j < channels; ++j)
            {
                Assets::FRigChannel channel;
                channel.bone = reader.Count(bones - 1);
                reader.Keys(channel.position, &RigBinary::Reader::Vector);
                reader.Keys(channel.rotation, &RigBinary::Reader::Rotation);
                reader.Keys(channel.scale, &RigBinary::Reader::Vector);
                clip.channels.push_back(std::move(channel));
            }
            rig.clips.push_back(std::move(clip));
        }
        if (!reader.Done()) throw std::runtime_error("Unexpected data after rig");
        return rig;
    }
}
