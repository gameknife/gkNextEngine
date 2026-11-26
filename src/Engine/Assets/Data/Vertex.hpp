#pragma once

#include "Engine/Utilities/Glm.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Assets
{
    struct Vertex final
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec4 Tangent;
        glm::vec2 TexCoord;
        glm::uint MaterialIndex;

        bool operator==(const Vertex& other) const
        {
            return 
                Position == other.Position &&
                Normal == other.Normal &&
                Tangent == other.Tangent &&
                TexCoord == other.TexCoord &&
                MaterialIndex == other.MaterialIndex;
        }
    };
    
    inline uint16_t packUint8(uint16_t a, uint16_t b) {
        return a << 8 | b;
    }

    // ------------------------------------------------------------------------
    // Vertex shading payload. Tangent.w keeps the handedness in its sign and
    // carries 6 bits in its magnitude, w = sign * (1 + payload / 64):
    //   bit 0     macro variation (world-space low-frequency albedo breakup)
    //   bits 1..5 baked occlusion level 0..31 -> darkening 0..kVertexMaxOcclusion
    // Every loader that writes w = +-1 gets payload 0 (no occlusion, no
    // variation). On the GPU the payload sits in bits 2..7 of the high byte of
    // GPUVertex::tangentw (UnpackVertex in common/ConstFunc.slang).
    // ------------------------------------------------------------------------
    inline constexpr float kVertexMaxOcclusion = 0.45f;

    inline uint32_t TangentWPayload(float w)
    {
        const float code = (std::abs(w) - 1.0f) * 64.0f;
        return static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(code)), 0, 63));
    }

    // ambientOcclusion in [1 - kVertexMaxOcclusion, 1]: 1 = unoccluded.
    inline float EncodeTangentW(float handedness, float ambientOcclusion, bool macroVariation)
    {
        const float darkening = std::clamp(1.0f - ambientOcclusion, 0.0f, kVertexMaxOcclusion);
        const uint32_t level = static_cast<uint32_t>(std::lround(darkening / kVertexMaxOcclusion * 31.0f));
        const uint32_t payload = (level << 1u) | (macroVariation ? 1u : 0u);
        return (handedness < 0.0f ? -1.0f : 1.0f) * (1.0f + static_cast<float>(payload) / 64.0f);
    }

    inline float TangentWOcclusion(float w)
    {
        return 1.0f - static_cast<float>(TangentWPayload(w) >> 1u) / 31.0f * kVertexMaxOcclusion;
    }
    
    struct GPUVertex final
    {
        glm::detail::hdata posx;
        glm::detail::hdata posy;
        glm::detail::hdata posz;
        glm::detail::hdata texcoordx;
        
        glm::detail::hdata normalx;
        glm::detail::hdata normaly;
        glm::detail::hdata normalz;
        glm::detail::hdata texcoordy;
        
        glm::detail::hdata tangentx;
        glm::detail::hdata tangenty;
        glm::detail::hdata tangentz;
        uint16_t tangentw;
        
        static VkVertexInputBindingDescription GetBindingDescription()
        {
            VkVertexInputBindingDescription bindingDescription = {};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(GPUVertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }

        static VkVertexInputBindingDescription GetFastBindingDescription()
        {
            VkVertexInputBindingDescription bindingDescription = {};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(short) * 4;
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }
        
        static std::array<VkVertexInputAttributeDescription, 1> GetFastAttributeDescriptions()
        {
            std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions = {};

            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
            attributeDescriptions[0].offset = 0;
            
            return attributeDescriptions;
        }
    };

    inline GPUVertex MakeVertex(Vertex& cpuVertex)
    {
        GPUVertex vertex;
        vertex.posx = glm::detail::toFloat16(cpuVertex.Position.x);
        vertex.posy = glm::detail::toFloat16(cpuVertex.Position.y);
        vertex.posz = glm::detail::toFloat16(cpuVertex.Position.z);
        vertex.texcoordx = glm::detail::toFloat16(cpuVertex.TexCoord.x);
        
        vertex.normalx = glm::detail::toFloat16(cpuVertex.Normal.x);
        vertex.normaly = glm::detail::toFloat16(cpuVertex.Normal.y);
        vertex.normalz = glm::detail::toFloat16(cpuVertex.Normal.z);
        vertex.texcoordy = glm::detail::toFloat16(cpuVertex.TexCoord.y);

        vertex.tangentx = glm::detail::toFloat16(cpuVertex.Tangent.x);
        vertex.tangenty = glm::detail::toFloat16(cpuVertex.Tangent.y);
        vertex.tangentz = glm::detail::toFloat16(cpuVertex.Tangent.z);

        // High byte: bit 1 handedness, bits 2..7 shading payload; low byte: material slot.
        const uint16_t high = static_cast<uint16_t>((cpuVertex.Tangent.w > 0 ? 2u : 0u) |
                                                    (TangentWPayload(cpuVertex.Tangent.w) << 2u));
        vertex.tangentw = packUint8(high, static_cast<uint16_t>(cpuVertex.MaterialIndex & 0xFFu));
        return vertex;
    }

}
