#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Assets/Loaders/TextureGraph.hpp"
#include "Engine/Assets/Loaders/ProceduralNoise.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace Assets
{
    enum class FTextureGraph::EOp : uint8_t
    {
        Const,
        U,
        V,
        Fbm,
        Cells,
        Course,
        Weave,
        Stripes,
        Checker,
        Hash,
        Palette,
        Abs,
        Fract,
        Invert,
        Posterize,
        Smoothstep,
        Step,
        Band,
        Pow,
        Add,
        Sub,
        Mul,
        Min,
        Max,
        Mix,
        Select,
        Top,
        Warp,
        Tile,
        Shear,
        Pixelate,
        Offset,
        Surface,
    };

    // Named (not anonymous) so unity builds cannot collide these helpers.
    namespace TextureGraphDetail
    {
        using EOp = FTextureGraph::EOp;

        // Signature characters: n = number, e = expression, s = mode string,
        // c = colour constant [r,g,b], + = two or more expressions.
        struct FOpInfo
        {
            const char* name;
            EOp op;
            const char* signature;
            std::vector<const char*> modes;
            const char* usage;
        };

        const std::vector<FOpInfo>& Ops()
        {
            static const std::vector<FOpInfo> ops = {
                {"u", EOp::U, "", {}, "[\"u\"]"},
                {"v", EOp::V, "", {}, "[\"v\"]"},
                {"fbm", EOp::Fbm, "nnnn", {}, "[\"fbm\", cellsU, cellsV, octaves, seed]"},
                {"cells", EOp::Cells, "nns", {"dist", "edge", "rand"}, "[\"cells\", count, seed, \"dist\"|\"edge\"|\"rand\"]"},
                {"course", EOp::Course, "nnnns", {"fu", "fv", "edge", "rand", "row", "col"},
                 "[\"course\", rows, columns, shift (<0 random), seed, \"fu\"|\"fv\"|\"edge\"|\"rand\"|\"row\"|\"col\"]"},
                {"weave", EOp::Weave, "nn", {}, "[\"weave\", threads, twill (0/1)]"},
                {"stripes", EOp::Stripes, "nn", {}, "[\"stripes\", countU, countV]"},
                {"checker", EOp::Checker, "nn", {}, "[\"checker\", countU, countV]"},
                {"hash", EOp::Hash, "nnn", {}, "[\"hash\", cellsU, cellsV, seed]"},
                {"palette", EOp::Palette, "nc", {}, "[\"palette\", index, [r, g, b]]"},
                {"abs", EOp::Abs, "e", {}, "[\"abs\", a]"},
                {"fract", EOp::Fract, "e", {}, "[\"fract\", a]"},
                {"invert", EOp::Invert, "e", {}, "[\"invert\", a]"},
                {"posterize", EOp::Posterize, "en", {}, "[\"posterize\", a, levels]"},
                {"smoothstep", EOp::Smoothstep, "enn", {}, "[\"smoothstep\", a, edge0, edge1]"},
                {"step", EOp::Step, "en", {}, "[\"step\", a, edge]"},
                {"band", EOp::Band, "enn", {}, "[\"band\", a, center, halfWidth]"},
                {"pow", EOp::Pow, "en", {}, "[\"pow\", a, exponent]"},
                {"add", EOp::Add, "+", {}, "[\"add\", a, b, ...]"},
                {"sub", EOp::Sub, "ee", {}, "[\"sub\", a, b]"},
                {"mul", EOp::Mul, "+", {}, "[\"mul\", a, b, ...]"},
                {"min", EOp::Min, "+", {}, "[\"min\", a, b, ...]"},
                {"max", EOp::Max, "+", {}, "[\"max\", a, b, ...]"},
                {"mix", EOp::Mix, "eee", {}, "[\"mix\", a, b, t]"},
                {"select", EOp::Select, "eee", {}, "[\"select\", mask, ifSet, ifClear]"},
                {"top", EOp::Top, "en", {}, "[\"top\", field, fraction]"},
                {"warp", EOp::Warp, "eeen", {}, "[\"warp\", field, du, dv, amount]"},
                {"tile", EOp::Tile, "enn", {}, "[\"tile\", field, repeatsU, repeatsV]"},
                {"shear", EOp::Shear, "en", {}, "[\"shear\", field, k]"},
                {"pixelate", EOp::Pixelate, "en", {}, "[\"pixelate\", field, cells]"},
                {"offset", EOp::Offset, "enn", {}, "[\"offset\", field, du, dv]"},
                {"surface", EOp::Surface, "eeeenn", {}, "[\"surface\", color, height, rough, metal, tilt, blur]"},
            };
            return ops;
        }

        std::string FormatNumber(double v)
        {
            return fmt::format("{:.6g}", v);
        }

        float Fract(float x)
        {
            return x - std::floor(x);
        }

        int LatticeCount(float v, int lo, int hi)
        {
            return std::clamp(static_cast<int>(std::lround(v)), lo, hi);
        }
    }

    using namespace TextureGraphDetail;
    using namespace ProceduralNoise;

    struct FTextureGraph::FEvalState
    {
        uint32_t size = 0;
        uint32_t seed = 0;
        std::span<const glm::vec3> palette;
        std::vector<glm::vec2> lastUv;
        std::vector<glm::vec3> lastValue;
        std::vector<float> cut;
        std::vector<uint8_t> cutReady;
    };

    struct FTextureGraph::FCompiler
    {
        FTextureGraph& graph;
        std::string& error;
        std::unordered_map<std::string, int32_t> byCanonical;
        std::vector<std::string> canonical;
        size_t paletteCount = 0;

        int32_t Intern(FNode node, std::string text)
        {
            if (const auto found = byCanonical.find(text); found != byCanonical.end()) return found->second;
            const int32_t index = static_cast<int32_t>(graph.nodes_.size());
            if (node.op == EOp::Palette)
            {
                const size_t slot = static_cast<size_t>(node.num[0]);
                if (graph.defaultPalette_.size() <= slot) graph.defaultPalette_.resize(slot + 1, glm::vec3(1.0f));
                graph.defaultPalette_[slot] = node.constant;
            }
            graph.nodes_.push_back(std::move(node));
            canonical.push_back(text);
            byCanonical.emplace(std::move(text), index);
            return index;
        }

        static bool AsColor(const FTexExpr& e, glm::vec3& out)
        {
            if (e.kind != FTexExpr::EKind::List || e.items.size() != 3) return false;
            for (int i = 0; i < 3; ++i)
            {
                if (e.items[static_cast<size_t>(i)].kind != FTexExpr::EKind::Number) return false;
                out[i] = static_cast<float>(e.items[static_cast<size_t>(i)].number);
            }
            return true;
        }

        bool Fail(const std::string& message)
        {
            if (error.empty()) error = message;
            return false;
        }

        int32_t Expression(const FTexExpr& e, bool allowSurface)
        {
            if (e.kind == FTexExpr::EKind::Number)
            {
                FNode node;
                node.op = EOp::Const;
                node.constant = glm::vec3(static_cast<float>(e.number));
                return Intern(std::move(node), FormatNumber(e.number));
            }
            glm::vec3 color;
            if (AsColor(e, color))
            {
                FNode node;
                node.op = EOp::Const;
                node.constant = color;
                return Intern(std::move(node), fmt::format("[{} {} {}]", FormatNumber(color.r), FormatNumber(color.g),
                                                           FormatNumber(color.b)));
            }
            if (e.kind == FTexExpr::EKind::Nil)
            {
                Fail("texture graph: got undef (an argument left unset, or a function that does not exist?)");
                return -1;
            }
            if (e.kind != FTexExpr::EKind::List || e.items.empty() || e.items[0].kind != FTexExpr::EKind::String)
            {
                Fail("texture graph: expected a number, an [r, g, b] colour or a [\"node\", ...] list");
                return -1;
            }
            const std::string& name = e.items[0].text;
            const auto& ops = Ops();
            const auto info = std::find_if(ops.begin(), ops.end(), [&](const FOpInfo& op) { return name == op.name; });
            if (info == ops.end())
            {
                std::string known;
                for (const FOpInfo& op : ops) known += (known.empty() ? "" : ", ") + std::string(op.name);
                Fail(fmt::format("texture graph: unknown node '{}' (known: {})", name, known));
                return -1;
            }
            if (info->op == EOp::Surface && !allowSurface)
            {
                Fail("texture graph: 'surface' may only be the root");
                return -1;
            }

            FNode node;
            node.op = info->op;
            const std::string_view signature = info->signature;
            const size_t argCount = e.items.size() - 1;
            const bool variadic = signature == "+";
            if ((variadic && argCount < 2) || (!variadic && argCount != signature.size()))
            {
                Fail(fmt::format("texture graph: '{}' takes {} -- got {} argument(s)", name, info->usage, argCount));
                return -1;
            }
            std::string text = "(" + name;
            size_t numberSlot = 0;
            for (size_t i = 0; i < argCount; ++i)
            {
                const FTexExpr& arg = e.items[i + 1];
                const char kind = variadic ? 'e' : signature[i];
                if (kind == 'n')
                {
                    if (arg.kind != FTexExpr::EKind::Number)
                    {
                        Fail(fmt::format("texture graph: argument {} of '{}' must be a number ({})", i + 1, name, info->usage));
                        return -1;
                    }
                    node.num[numberSlot++] = static_cast<float>(arg.number);
                    text += " " + FormatNumber(arg.number);
                }
                else if (kind == 's')
                {
                    const auto mode = arg.kind == FTexExpr::EKind::String
                        ? std::find_if(info->modes.begin(), info->modes.end(), [&](const char* m) { return arg.text == m; })
                        : info->modes.end();
                    if (mode == info->modes.end())
                    {
                        Fail(fmt::format("texture graph: '{}' has no mode '{}' ({})", name, arg.text, info->usage));
                        return -1;
                    }
                    node.mode = static_cast<uint8_t>(mode - info->modes.begin());
                    text += " \"" + arg.text + "\"";
                }
                else if (kind == 'c')
                {
                    if (!AsColor(arg, node.constant))
                    {
                        Fail(fmt::format("texture graph: argument {} of '{}' must be [r, g, b] ({})", i + 1, name, info->usage));
                        return -1;
                    }
                    text += fmt::format(" [{} {} {}]", FormatNumber(node.constant.r), FormatNumber(node.constant.g),
                                        FormatNumber(node.constant.b));
                }
                else
                {
                    const int32_t input = Expression(arg, false);
                    if (input < 0)
                    {
                        // Breadcrumbs from the failing node up to the root.
                        error += fmt::format(" <- argument {} of '{}'", i + 1, name);
                        return -1;
                    }
                    node.inputs.push_back(input);
                    text += " " + canonical[static_cast<size_t>(input)];
                }
            }
            text += ")";
            if (!Validate(node, name, info->usage)) return -1;
            return Intern(std::move(node), std::move(text));
        }

        // Lattice counts must be integers in range: that is what keeps a graph
        // tile-periodic and its evaluation bounded.
        bool Validate(FNode& node, const std::string& name, const char* usage)
        {
            const auto range = [&](size_t slot, int lo, int hi, const char* what)
            {
                const float v = node.num[slot];
                if (v < static_cast<float>(lo) - 0.5f || v > static_cast<float>(hi) + 0.5f)
                    return Fail(fmt::format("texture graph: '{}' {} must be {}..{} ({})", name, what, lo, hi, usage));
                node.num[slot] = static_cast<float>(LatticeCount(v, lo, hi));
                return true;
            };
            switch (node.op)
            {
            case EOp::Fbm: return range(0, 1, 256, "cellsU") && range(1, 1, 256, "cellsV") && range(2, 1, 8, "octaves");
            case EOp::Cells: return range(0, 1, 256, "count");
            case EOp::Course: return range(0, 1, 256, "rows") && range(1, 1, 256, "columns");
            case EOp::Weave: return range(0, 1, 256, "threads");
            case EOp::Stripes: return range(0, -256, 256, "countU") && range(1, -256, 256, "countV");
            case EOp::Checker: return range(0, 1, 256, "countU") && range(1, 1, 256, "countV");
            case EOp::Hash: return range(0, 1, 2048, "cellsU") && range(1, 1, 2048, "cellsV");
            case EOp::Palette: return range(0, 0, 15, "index");
            case EOp::Posterize: return range(0, 1, 64, "levels");
            case EOp::Tile: return range(0, 1, 64, "repeatsU") && range(1, 1, 64, "repeatsV");
            case EOp::Shear: return range(0, -16, 16, "k");
            case EOp::Pixelate: return range(0, 1, 2048, "cells");
            case EOp::Top:
                if (node.num[0] < 0.0f || node.num[0] > 1.0f)
                    return Fail(fmt::format("texture graph: 'top' fraction must be 0..1 ({})", usage));
                return true;
            case EOp::Surface:
                node.num[0] = std::clamp(node.num[0], 0.0f, 40.0f);
                return range(1, 0, 4, "blur");
            default: return true;
            }
        }
    };

    std::shared_ptr<const FTextureGraph> FTextureGraph::Compile(const FTexExpr& root, std::string& error)
    {
        error.clear();
        auto graph = std::make_shared<FTextureGraph>();
        FCompiler compiler{*graph, error};
        if (root.kind != FTexExpr::EKind::List || root.items.empty() || root.items[0].kind != FTexExpr::EKind::String ||
            root.items[0].text != "surface")
        {
            error = "texture graph: the root must be [\"surface\", color, height, rough, metal, tilt, blur] (tx_surface())";
            return nullptr;
        }
        const int32_t rootIndex = compiler.Expression(root, true);
        if (rootIndex < 0) return nullptr;
        const FNode& surface = graph->nodes_[static_cast<size_t>(rootIndex)];
        graph->color_ = surface.inputs[0];
        graph->height_ = surface.inputs[1];
        graph->rough_ = surface.inputs[2];
        graph->metal_ = surface.inputs[3];
        graph->tilt_ = surface.num[0];
        graph->blur_ = static_cast<int>(surface.num[1]);
        graph->canonical_ = compiler.canonical[static_cast<size_t>(rootIndex)];
        return graph;
    }

    glm::vec3 FTextureGraph::Eval(int32_t index, glm::vec2 uv, FEvalState& state) const
    {
        const size_t i = static_cast<size_t>(index);
        if (state.lastUv[i] == uv) return state.lastValue[i];
        const FNode& n = nodes_[i];
        const auto in = [&](size_t k, glm::vec2 at) { return Eval(n.inputs[k], at, state); };
        const auto seedOf = [&](float authored)
        {
            return Mix32(static_cast<uint32_t>(static_cast<int64_t>(authored)) * 0x9E3779B1U ^ state.seed);
        };
        glm::vec3 r(0.0f);
        switch (n.op)
        {
        case EOp::Const: r = n.constant; break;
        case EOp::U: r = glm::vec3(Fract(uv.x)); break;
        case EOp::V: r = glm::vec3(Fract(uv.y)); break;
        case EOp::Fbm:
            r = glm::vec3(Fbm(uv.x, uv.y, static_cast<int>(n.num[0]), static_cast<int>(n.num[1]), static_cast<int>(n.num[2]),
                              seedOf(n.num[3])));
            break;
        case EOp::Cells:
        {
            const FCellHit hit = Cellular(uv.x, uv.y, static_cast<int>(n.num[0]), seedOf(n.num[1]));
            r = glm::vec3(n.mode == 0 ? hit.dist : n.mode == 1 ? hit.edge : Rand01(hit.id, 11U, seedOf(n.num[1])));
            break;
        }
        case EOp::Course:
        {
            const int rows = static_cast<int>(n.num[0]);
            const int columns = static_cast<int>(n.num[1]);
            const bool randomShift = n.num[2] < 0.0f;
            const uint32_t seed = seedOf(n.num[3]);
            // Phased by a fraction of a unit, so no joint runs along the tile edge.
            const FCourseHit unit = Course(uv.x + 0.25f / static_cast<float>(columns),
                                           uv.y + 0.5f / static_cast<float>(rows), rows, columns,
                                           randomShift ? 0.0f : n.num[2], seed, randomShift);
            float value = 0.0f;
            switch (n.mode)
            {
            case 0: value = unit.fu; break;
            case 1: value = unit.fv; break;
            case 2:
            {
                // Distance to the nearest joint, in unit heights.
                const float aspect = static_cast<float>(rows) / static_cast<float>(columns);
                value = std::min(std::min(unit.fu, 1.0f - unit.fu) * aspect, std::min(unit.fv, 1.0f - unit.fv));
                break;
            }
            case 3: value = Rand01(static_cast<uint32_t>(unit.row * 4099 + unit.column), 5U, seed); break;
            case 4: value = (static_cast<float>(unit.row) + 0.5f) / static_cast<float>(rows); break;
            default: value = (static_cast<float>(unit.column) + 0.5f) / static_cast<float>(columns); break;
            }
            r = glm::vec3(value);
            break;
        }
        case EOp::Weave: r = glm::vec3(Weave(uv.x, uv.y, n.num[0], n.num[1] > 0.5f)); break;
        case EOp::Stripes: r = glm::vec3(Fract(uv.x * n.num[0] + uv.y * n.num[1])); break;
        case EOp::Checker:
        {
            const int cx = static_cast<int>(std::floor(uv.x * n.num[0]));
            const int cy = static_cast<int>(std::floor(uv.y * n.num[1]));
            r = glm::vec3(((cx + cy) & 1) != 0 ? 1.0f : 0.0f);
            break;
        }
        case EOp::Hash:
        {
            const int cu = static_cast<int>(n.num[0]);
            const int cv = static_cast<int>(n.num[1]);
            const int cx = WrapIndex(static_cast<int>(std::floor(uv.x * static_cast<float>(cu))), cu);
            const int cy = WrapIndex(static_cast<int>(std::floor(uv.y * static_cast<float>(cv))), cv);
            r = glm::vec3(Rand01(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy), seedOf(n.num[2])));
            break;
        }
        case EOp::Palette:
        {
            const size_t slot = static_cast<size_t>(n.num[0]);
            r = slot < state.palette.size() ? state.palette[slot] : n.constant;
            break;
        }
        case EOp::Abs: r = glm::abs(in(0, uv)); break;
        case EOp::Fract: r = glm::fract(in(0, uv)); break;
        case EOp::Invert: r = 1.0f - in(0, uv); break;
        case EOp::Posterize:
        {
            const glm::vec3 a = in(0, uv);
            const int levels = static_cast<int>(n.num[0]);
            r = glm::vec3(Posterize(a.x, levels), Posterize(a.y, levels), Posterize(a.z, levels));
            break;
        }
        case EOp::Smoothstep: r = glm::smoothstep(glm::vec3(n.num[0]), glm::vec3(n.num[1]), in(0, uv)); break;
        case EOp::Step: r = glm::step(glm::vec3(n.num[0]), in(0, uv)); break;
        case EOp::Band:
            r = glm::max(glm::vec3(0.0f), 1.0f - glm::abs(in(0, uv) - n.num[0]) / std::max(n.num[1], 1e-5f));
            break;
        case EOp::Pow: r = glm::pow(glm::max(in(0, uv), glm::vec3(0.0f)), glm::vec3(n.num[0])); break;
        case EOp::Add:
        case EOp::Mul:
        case EOp::Min:
        case EOp::Max:
        {
            r = in(0, uv);
            for (size_t k = 1; k < n.inputs.size(); ++k)
            {
                const glm::vec3 b = in(k, uv);
                r = n.op == EOp::Add ? r + b : n.op == EOp::Mul ? r * b : n.op == EOp::Min ? glm::min(r, b) : glm::max(r, b);
            }
            break;
        }
        case EOp::Sub: r = in(0, uv) - in(1, uv); break;
        case EOp::Mix: r = glm::mix(in(0, uv), in(1, uv), in(2, uv)); break;
        case EOp::Select: r = in(0, uv).x > 0.5f ? in(1, uv) : in(2, uv); break;
        case EOp::Top:
        {
            // A cut by rank over the whole tile: exactly `fraction` of it is set,
            // whatever the statistics of the field below.
            if (!state.cutReady[i])
            {
                std::vector<float> values;
                values.reserve(static_cast<size_t>(state.size) * state.size);
                for (uint32_t y = 0; y < state.size; ++y)
                    for (uint32_t x = 0; x < state.size; ++x)
                        values.push_back(in(0, glm::vec2((static_cast<float>(x) + 0.5f) / static_cast<float>(state.size),
                                                         (static_cast<float>(y) + 0.5f) / static_cast<float>(state.size))).x);
                const size_t rank = std::min(values.size() - 1, static_cast<size_t>(
                    std::clamp(1.0f - n.num[0], 0.0f, 1.0f) * static_cast<float>(values.size())));
                std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(rank), values.end());
                state.cut[i] = n.num[0] <= 0.0f ? std::numeric_limits<float>::infinity() : values[rank];
                state.cutReady[i] = 1;
                // The pre-pass evaluated this node's inputs elsewhere; the memo
                // is keyed by uv, so the texel below still evaluates correctly.
            }
            r = glm::vec3(in(0, uv).x > state.cut[i] || (n.num[0] >= 1.0f) ? 1.0f : 0.0f);
            break;
        }
        case EOp::Warp:
        {
            const float du = in(1, uv).x - 0.5f;
            const float dv = in(2, uv).x - 0.5f;
            r = in(0, uv + glm::vec2(du, dv) * n.num[0]);
            break;
        }
        case EOp::Tile: r = in(0, glm::vec2(Fract(uv.x * n.num[0]), Fract(uv.y * n.num[1]))); break;
        case EOp::Shear: r = in(0, glm::vec2(Fract(uv.x + n.num[0] * uv.y), uv.y)); break;
        case EOp::Pixelate:
        {
            const float cells = n.num[0];
            r = in(0, (glm::floor(uv * cells) + 0.5f) / cells);
            break;
        }
        case EOp::Offset: r = in(0, uv + glm::vec2(n.num[0], n.num[1])); break;
        case EOp::Surface: break;
        }
        state.lastUv[i] = uv;
        state.lastValue[i] = r;
        return r;
    }

    void FTextureGraph::Bake(uint32_t size, std::span<const glm::vec3> palette, uint32_t seed, FTextureGraphBake& out) const
    {
        FEvalState state;
        state.size = size;
        state.seed = Mix32(seed * 0x2545F491U + 0x9E37U);
        state.palette = palette;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        state.lastUv.assign(nodes_.size(), glm::vec2(nan));
        state.lastValue.assign(nodes_.size(), glm::vec3(0.0f));
        state.cut.assign(nodes_.size(), 0.0f);
        state.cutReady.assign(nodes_.size(), 0);
        const size_t texels = static_cast<size_t>(size) * size;
        out.color.resize(texels);
        out.height.resize(texels);
        out.rough.resize(texels);
        out.metal.resize(texels);
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const glm::vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                   (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
                const size_t t = static_cast<size_t>(y) * size + x;
                out.color[t] = glm::clamp(Eval(color_, uv, state), glm::vec3(0.0f), glm::vec3(1.0f));
                out.height[t] = Eval(height_, uv, state).x;
                out.rough[t] = std::clamp(Eval(rough_, uv, state).x, 0.0f, 1.0f);
                out.metal[t] = std::clamp(Eval(metal_, uv, state).x, 0.0f, 1.0f);
            }
    }
}
