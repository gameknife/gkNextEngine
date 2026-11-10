#pragma once

// ============================================================================
// TextureGraph.hpp - Procedural prints authored as data (SCAD), evaluated natively.
//
// Every gk_texture print is a texture graph, written in SCAD: a tree of
// nodes -- periodic noise, cells, brick courses, weave, stripes, arithmetic,
// coverage cuts, domain warps -- whose root
// `surface` node names four channels: colour, relief height, roughness factor
// and metalness factor. SCAD builds the tree with plain functions
// (assets/scad/lib/gk_texgraph.scad) that return lists such as
// ["fbm", 4, 4, 3, 11]; the loader hands the list over as an FTexExpr, the
// graph compiles it once (validating every node) and bakes it per texel in
// C++, so a new print costs no engine change and no interpreter time.
//
// Every node evaluates to an RGB triple; scalar fields carry the same value in
// all three components and constants broadcast, so colour and scalar math mix
// freely. Lattice counts are integers, which keeps every graph tile-periodic.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Assets
{
    // An authored expression: a number, a string or a list of expressions
    // (the shape of SCAD values, without depending on the SCAD module).
    struct FTexExpr
    {
        enum class EKind : uint8_t
        {
            Nil,
            Number,
            String,
            List,
        };
        EKind kind = EKind::Nil;
        double number = 0.0;
        std::string text;
        std::vector<FTexExpr> items;
    };

    // Per-texel channels of one bake, row-major size x size.
    struct FTextureGraphBake
    {
        std::vector<glm::vec3> color; // linear RGB
        std::vector<float> height;    // relief; only its slopes matter
        std::vector<float> rough;     // roughness factor 0..1
        std::vector<float> metal;     // metalness factor 0..1
    };

    class FTextureGraph
    {
    public:
        // `root` must be ["surface", color, height, rough, metal, tilt, blur].
        // Returns null and fills `error` (naming the offending node) on a bad graph.
        static std::shared_ptr<const FTextureGraph> Compile(const FTexExpr& root, std::string& error);

        // Canonical text of the graph: the content key (equal graphs, equal text).
        const std::string& Canonical() const { return canonical_; }
        // Relief slope (degrees at the 90th percentile) and bevel blur, from `surface`.
        float ReliefTilt() const { return tilt_; }
        int ReliefBlur() const { return blur_; }
        // Defaults of the ["palette", i, [r,g,b]] nodes, indexed by i.
        const std::vector<glm::vec3>& DefaultPalette() const { return defaultPalette_; }
        size_t NodeCount() const { return nodes_.size(); }

        // Evaluates every texel. `palette` overrides the defaults by index;
        // `seed` re-rolls every noise node of the graph at once.
        void Bake(uint32_t size, std::span<const glm::vec3> palette, uint32_t seed, FTextureGraphBake& out) const;

        enum class EOp : uint8_t;

    private:
        struct FNode
        {
            EOp op{};
            std::array<float, 4> num{};   // numeric arguments, in signature order
            std::vector<int32_t> inputs;  // expression arguments, in signature order
            glm::vec3 constant{0.0f};     // Const value / Palette default
            uint8_t mode = 0;             // string argument, as an index into the op's modes
        };
        struct FEvalState;
        struct FCompiler;

        glm::vec3 Eval(int32_t index, glm::vec2 uv, FEvalState& state) const;

        std::vector<FNode> nodes_;
        int32_t color_ = -1;
        int32_t height_ = -1;
        int32_t rough_ = -1;
        int32_t metal_ = -1;
        float tilt_ = 10.0f;
        int blur_ = 1;
        std::vector<glm::vec3> defaultPalette_;
        std::string canonical_;
    };
}
