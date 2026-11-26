#include "Modules/ScadLoader/FScadShared.h"

#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"
#if !defined(__EMSCRIPTEN__)
#include "Engine/Utilities/FileHelper.hpp"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <unordered_set>

#include <fmt/format.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Assets::Scad
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path looseSourceRoot;
        std::mutex sidecarMutex;
        std::vector<std::string> sidecarPaths;

        bool ScadReadBytes(const fs::path& path, std::vector<uint8_t>& data)
        {
#if !defined(__EMSCRIPTEN__)
            if (auto* package = Utilities::Package::FPackageFileSystem::TryGetInstance(); package && looseSourceRoot.empty())
            {
                return package->LoadFile(path.generic_string(), data);
            }
#endif
            std::ifstream file(ResolveScadLoosePath(path), std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }
            data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return true;
        }

        bool ScadReadFile(const fs::path& path, std::string& out)
        {
            std::vector<uint8_t> data;
            if (!ScadReadBytes(path, data))
            {
                return false;
            }
            out.assign(reinterpret_cast<const char*>(data.data()), data.size());
            return true;
        }

        bool ScadIsIdentChar(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        // Extracts `use <path>` / `include <path>` directives and blanks them
        // out of the source (preserving offsets/line numbers) so the lexer
        // never sees them.
        struct ScadDirective
        {
            bool isInclude = false;
            std::string path;
        };

        std::vector<ScadDirective> ScadExtractDirectives(const std::string& source, std::string& stripped)
        {
            std::vector<ScadDirective> directives;
            stripped = source;

            auto tryMatch = [&](size_t i, const char* keyword, bool isInclude) -> size_t
            {
                const size_t kwLen = std::char_traits<char>::length(keyword);
                if (source.compare(i, kwLen, keyword) != 0)
                {
                    return 0;
                }
                if (i > 0 && ScadIsIdentChar(source[i - 1]))
                {
                    return 0; // part of a larger identifier
                }
                size_t j = i + kwLen;
                if (j < source.size() && ScadIsIdentChar(source[j]))
                {
                    return 0;
                }
                while (j < source.size() && (source[j] == ' ' || source[j] == '\t'))
                {
                    ++j;
                }
                if (j >= source.size() || source[j] != '<')
                {
                    return 0;
                }
                const size_t pathStart = j + 1;
                const size_t pathEnd = source.find('>', pathStart);
                if (pathEnd == std::string::npos)
                {
                    return 0;
                }
                ScadDirective d;
                d.isInclude = isInclude;
                d.path = source.substr(pathStart, pathEnd - pathStart);
                directives.push_back(d);
                for (size_t k = i; k <= pathEnd; ++k)
                {
                    if (stripped[k] != '\n')
                    {
                        stripped[k] = ' ';
                    }
                }
                return pathEnd + 1;
            };

            for (size_t i = 0; i < source.size();)
            {
                if (source[i] == '"')
                {
                    ++i;
                    while (i < source.size())
                    {
                        if (source[i] == '\\' && i + 1 < source.size())
                        {
                            i += 2;
                            continue;
                        }
                        if (source[i++] == '"')
                        {
                            break;
                        }
                    }
                    continue;
                }
                if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/')
                {
                    i += 2;
                    while (i < source.size() && source[i] != '\n')
                    {
                        ++i;
                    }
                    continue;
                }
                if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '*')
                {
                    i += 2;
                    while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/'))
                    {
                        ++i;
                    }
                    i = std::min(i + 2, source.size());
                    continue;
                }

                size_t next = tryMatch(i, "use", false);
                if (next == 0)
                {
                    next = tryMatch(i, "include", true);
                }
                i = (next != 0) ? next : i + 1;
            }
            return directives;
        }

        struct ScadPosKey
        {
            // Built with MakePosKey: -0 and +0 compare equal but hash apart.
            float x, y, z;
            bool operator==(const ScadPosKey& o) const { return x == o.x && y == o.y && z == o.z; }
        };

        struct ScadPosKeyHash
        {
            size_t operator()(const ScadPosKey& k) const
            {
                uint32_t bits[3];
                std::memcpy(&bits[0], &k.x, sizeof(float));
                std::memcpy(&bits[1], &k.y, sizeof(float));
                std::memcpy(&bits[2], &k.z, sizeof(float));
                size_t h = static_cast<size_t>(1469598103934665603ull);
                for (uint32_t b : bits)
                {
                    h ^= static_cast<size_t>(b);
                    h *= 1099511628211ull;
                }
                return h;
            }
        };

        ScadPosKey MakePosKey(const glm::vec3& p)
        {
            return ScadPosKey{p.x + 0.0f, p.y + 0.0f, p.z + 0.0f};
        }
    } // namespace

    fs::path ResolveScadLoosePath(const fs::path& path)
    {
#if defined(__EMSCRIPTEN__)
        return (looseSourceRoot / path).lexically_normal();
#else
        return looseSourceRoot.empty() ? Utilities::FileHelper::GetRuntimeFilePath(path)
                                       : (looseSourceRoot / path).lexically_normal();
#endif
    }

    bool ScadHasPackage()
    {
#if defined(__EMSCRIPTEN__)
        return false;
#else
        return Utilities::Package::FPackageFileSystem::TryGetInstance() != nullptr;
#endif
    }

    void SetLooseSourceRoot(fs::path root)
    {
        looseSourceRoot = std::move(root);
    }

    std::vector<std::string> LooseSourceSidecars()
    {
        const std::lock_guard lock(sidecarMutex);
        return sidecarPaths;
    }

    bool ScadReadAsset(const std::string& path, std::vector<uint8_t>& out)
    {
        const auto source = fs::path(path).lexically_normal();
        if (!looseSourceRoot.empty())
        {
            const auto absolute = ResolveScadLoosePath(source).string();
            const std::lock_guard lock(sidecarMutex);
            if (std::ranges::find(sidecarPaths, absolute) == sidecarPaths.end())
            {
                sidecarPaths.push_back(absolute);
            }
        }
        out.clear();
        return ScadReadBytes(source, out);
    }

    bool LoadScadProgram(const std::string& filename, ScadProgram& outProgram, std::string& outError,
                         std::vector<std::string>* outFiles)
    {
        fs::path mainPath = filename;
#if !defined(__EMSCRIPTEN__)
        if (mainPath.is_absolute() && looseSourceRoot.empty())
        {
            std::error_code ec;
            const std::array<fs::path, 2> roots = {
                Utilities::FileHelper::GetRuntimeRoot(),
                Utilities::FileHelper::GetWritableRuntimeRoot() / "asset-cache",
            };
            for (const fs::path& root : roots)
            {
                const fs::path relative = fs::relative(mainPath, root, ec);
                if (!ec && !relative.empty() && relative.generic_string().rfind("..", 0) != 0)
                {
                    mainPath = relative.lexically_normal();
                    break;
                }
                ec.clear();
            }
        }
#endif
        mainPath = mainPath.lexically_normal();

        std::unordered_set<std::string> parsedDefinitions;
        std::unordered_set<std::string> executedTopLevel;
        struct WorkItem
        {
            fs::path path;
            bool executable = false;
        };
        std::vector<WorkItem> queue;
        queue.push_back({mainPath, true});

        size_t head = 0;
        while (head < queue.size())
        {
            const WorkItem item = queue[head++];
            const std::string key = item.path.string();
            if (item.executable)
            {
                if (executedTopLevel.count(key) != 0)
                {
                    continue;
                }
            }
            else if (parsedDefinitions.count(key) != 0)
            {
                continue;
            }

            if (outFiles)
            {
                const fs::path absolute =
                    ResolveScadLoosePath(item.path);
                const std::string absoluteKey = absolute.lexically_normal().string();
                if (std::find(outFiles->begin(), outFiles->end(), absoluteKey) == outFiles->end())
                {
                    outFiles->push_back(absoluteKey);
                }
            }

            std::string source;
            if (!ScadReadFile(item.path, source))
            {
                outError = fmt::format("cannot read referenced file: {}", key);
                return false;
            }

            std::string stripped;
            std::vector<ScadDirective> directives = ScadExtractDirectives(source, stripped);

            std::vector<Token> tokens;
            std::string err;
            if (!ScadLexer::Tokenize(stripped, tokens, err))
            {
                outError = fmt::format("lex error in {}: {}", key, err);
                return false;
            }
            Scope scope;
            if (!ScadParser::Parse(tokens, scope, err))
            {
                outError = fmt::format("parse error in {}: {}", key, err);
                return false;
            }

            parsedDefinitions.insert(key);
            if (item.executable)
            {
                executedTopLevel.insert(key);
            }

            for (const StmtPtr& s : scope)
            {
                if (!s)
                {
                    continue;
                }
                if (s->kind == StmtKind::ModuleDef)
                {
                    outProgram.modules[s->name] = s;
                }
                else if (s->kind == StmtKind::FunctionDef)
                {
                    outProgram.functions[s->name] = s;
                }
                else if (item.executable)
                {
                    outProgram.mainTopLevel.push_back(s);
                }
            }

            const fs::path baseDir = item.path.parent_path();
            std::error_code ec;
            for (const ScadDirective& d : directives)
            {
                fs::path target = (baseDir / d.path).lexically_normal();
                if (target.is_absolute())
                {
                    target = fs::weakly_canonical(target, ec);
                }
                queue.push_back({target, d.isInclude});
            }
        }
        return true;
    }

    glm::vec3 ScadToWorldPos(const glm::dvec3& p, double scale)
    {
        return glm::vec3(
            static_cast<float>(p.x * scale),
            static_cast<float>(p.z * scale),
            static_cast<float>(-p.y * scale));
    }

    glm::dmat4 ScadToWorldBasis(double scale)
    {
        glm::dmat4 basis(1.0);
        basis[0] = glm::dvec4(scale, 0.0, 0.0, 0.0);
        basis[1] = glm::dvec4(0.0, 0.0, -scale, 0.0);
        basis[2] = glm::dvec4(0.0, scale, 0.0, 0.0);
        basis[3] = glm::dvec4(0.0, 0.0, 0.0, 1.0);
        return basis;
    }

    std::string ScadPlacementTag(size_t sourceOffset)
    {
        return fmt::format("scad-placement:{}", sourceOffset);
    }

    void ScadLocalToEngineTRS(
        const glm::dmat4& scadLocal,
        double scale,
        glm::vec3& outTranslation,
        glm::quat& outRotation,
        glm::vec3& outScale)
    {
        const glm::dmat4 basis = ScadToWorldBasis(scale);
        const glm::dmat4 engineLocal = basis * scadLocal * glm::inverse(basis);

        glm::dvec3 skew(0.0);
        glm::dvec4 perspective(0.0);
        glm::dvec3 scaleD(1.0);
        glm::dvec3 translationD(0.0);
        glm::dquat rotationD(1.0, 0.0, 0.0, 0.0);
        if (!glm::decompose(engineLocal, scaleD, rotationD, translationD, skew, perspective))
        {
            outTranslation = glm::vec3(0.0f);
            outRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            outScale = glm::vec3(1.0f);
            return;
        }

        outTranslation = glm::vec3(translationD);
        outRotation = glm::normalize(glm::quat(rotationD));
        outScale = glm::vec3(scaleD);
    }

    glm::dmat4 ScadRotateXYZ(const glm::dvec3& degrees)
    {
        glm::dmat4 r(1.0);
        r = glm::rotate(r, degrees.z * kDeg2Rad, glm::dvec3(0, 0, 1));
        r = glm::rotate(r, degrees.y * kDeg2Rad, glm::dvec3(0, 1, 0));
        r = glm::rotate(r, degrees.x * kDeg2Rad, glm::dvec3(1, 0, 0));
        return r;
    }

    std::vector<glm::vec3> ScadComputeSmoothNormals(const std::vector<glm::vec3>& pos, float angleThresholdDeg)
    {
        const size_t triCount = pos.size() / 3;
        if (angleThresholdDeg <= 0.0f)
        {
            std::vector<glm::vec3> out(pos.size(), glm::vec3(0.0f, 1.0f, 0.0f));
            for (size_t t = 0; t < triCount; ++t)
            {
                const glm::vec3 cross =
                    glm::cross(pos[t * 3 + 1] - pos[t * 3 + 0], pos[t * 3 + 2] - pos[t * 3 + 0]);
                const float len = glm::length(cross);
                const glm::vec3 normal = len > 1e-12f ? cross / len : glm::vec3(0.0f, 1.0f, 0.0f);
                out[t * 3 + 0] = normal;
                out[t * 3 + 1] = normal;
                out[t * 3 + 2] = normal;
            }
            return out;
        }

        std::vector<glm::vec3> weighted(triCount, glm::vec3(0.0f)); // raw cross (area weighted)
        std::vector<glm::vec3> unit(triCount, glm::vec3(0.0f, 1.0f, 0.0f));
        for (size_t t = 0; t < triCount; ++t)
        {
            const glm::vec3 cross = glm::cross(pos[t * 3 + 1] - pos[t * 3 + 0], pos[t * 3 + 2] - pos[t * 3 + 0]);
            weighted[t] = cross;
            const float len = glm::length(cross);
            if (len > 1e-12f) unit[t] = cross / len;
        }

        std::unordered_map<ScadPosKey, std::vector<uint32_t>, ScadPosKeyHash> adjacency;
        adjacency.reserve(pos.size());
        for (size_t i = 0; i < pos.size(); ++i)
        {
            adjacency[MakePosKey(pos[i])].push_back(static_cast<uint32_t>(i / 3));
        }

        const float cosThreshold = std::cos(angleThresholdDeg * 3.14159265358979323846f / 180.0f);
        std::vector<glm::vec3> out(pos.size(), glm::vec3(0.0f, 1.0f, 0.0f));
        for (size_t i = 0; i < pos.size(); ++i)
        {
            const uint32_t face = static_cast<uint32_t>(i / 3);
            const glm::vec3 fn = unit[face];
            glm::vec3 acc(0.0f);
            for (uint32_t other : adjacency[MakePosKey(pos[i])])
            {
                if (glm::dot(unit[other], fn) >= cosThreshold)
                {
                    acc += weighted[other];
                }
            }
            const float len = glm::length(acc);
            out[i] = (len > 1e-12f) ? acc / len : fn;
        }
        return out;
    }

    std::vector<glm::vec3> ScadComputeShadedNormals(const std::vector<glm::vec3>& pos, float defaultAngleDeg,
                                                    const std::vector<ScadNormalSpan>& spans,
                                                    const std::vector<glm::dvec3>& authored, double scale)
    {
        const bool hasAuthored = authored.size() == pos.size();
        if (spans.empty() && !hasAuthored)
        {
            return ScadComputeSmoothNormals(pos, defaultAngleDeg);
        }

        const size_t triCount = pos.size() / 3;
        const auto spanRange = [triCount](const ScadNormalSpan& span)
        {
            const size_t begin = std::min<size_t>(span.firstTri, triCount);
            return std::pair<size_t, size_t>(begin, std::min<size_t>(begin + span.triCount, triCount));
        };

        // Each triangle smooths with its own threshold. Triangles of different
        // thresholds never share a normal: a gk_crease subtree is its own piece.
        std::vector<float> triAngle(triCount, defaultAngleDeg);
        for (const ScadNormalSpan& span : spans)
        {
            const float angle =
                span.shading.smoothAngleDegrees >= 0.0f ? span.shading.smoothAngleDegrees : defaultAngleDeg;
            const auto [begin, end] = spanRange(span);
            std::fill(triAngle.begin() + static_cast<std::ptrdiff_t>(begin),
                      triAngle.begin() + static_cast<std::ptrdiff_t>(end), angle);
        }

        std::map<float, std::vector<uint32_t>> groups;
        for (size_t t = 0; t < triCount; ++t)
        {
            groups[triAngle[t]].push_back(static_cast<uint32_t>(t));
        }

        std::vector<glm::vec3> out;
        if (groups.size() == 1)
        {
            out = ScadComputeSmoothNormals(pos, groups.begin()->first);
        }
        else
        {
            out.assign(pos.size(), glm::vec3(0.0f, 1.0f, 0.0f));
            std::vector<glm::vec3> subset;
            for (const auto& [angle, tris] : groups)
            {
                subset.clear();
                subset.reserve(tris.size() * 3);
                for (uint32_t t : tris)
                {
                    subset.insert(subset.end(), {pos[t * 3 + 0], pos[t * 3 + 1], pos[t * 3 + 2]});
                }
                const std::vector<glm::vec3> normals = ScadComputeSmoothNormals(subset, angle);
                for (size_t k = 0; k < tris.size(); ++k)
                {
                    for (size_t corner = 0; corner < 3; ++corner)
                    {
                        out[tris[k] * 3 + corner] = normals[k * 3 + corner];
                    }
                }
            }
        }

        // Sculpt primitives know their surface: an authored normal replaces the
        // estimate from the neighbouring facets. SCAD (x, y, z) -> engine (x, z, -y).
        if (hasAuthored)
        {
            for (size_t i = 0; i < pos.size(); ++i)
            {
                const glm::dvec3& n = authored[i];
                const double length = glm::length(n);
                if (length > 1e-12)
                {
                    out[i] = glm::vec3(static_cast<float>(n.x / length), static_cast<float>(n.z / length),
                                       static_cast<float>(-n.y / length));
                }
            }
        }

        // gk_puff_normals: n' = normalize(mix(n, normalize(P - C + up * bias * R), strength)).
        for (const ScadNormalSpan& span : spans)
        {
            const ScadShading& shading = span.shading;
            if (!shading.puff || shading.puffStrength <= 0.0f)
            {
                continue;
            }
            const glm::vec3 center = ScadToWorldPos(shading.puffCenter, scale);
            const float radius = static_cast<float>(shading.puffRadius * scale);
            const glm::vec3 lift(0.0f, shading.puffUpBias * radius, 0.0f);
            const float strength = std::clamp(shading.puffStrength, 0.0f, 1.0f);
            const auto [begin, end] = spanRange(span);
            for (size_t i = begin * 3; i < end * 3; ++i)
            {
                const glm::vec3 radial = pos[i] - center + lift;
                const float radialLength = glm::length(radial);
                if (radialLength <= 1e-8f)
                {
                    continue;
                }
                const glm::vec3 bent = glm::mix(out[i], radial / radialLength, strength);
                const float bentLength = glm::length(bent);
                if (bentLength <= 1e-8f)
                {
                    continue;
                }
                out[i] = bent / bentLength;

                // A lobe face turned inward (toward the blob centre) would get a
                // normal facing away from its own triangle and shade black. Keep
                // the bent normal at least kMinFacing into the triangle's side.
                constexpr float kMinFacing = 0.25f;
                const size_t t = i / 3 * 3;
                const glm::vec3 cross = glm::cross(pos[t + 1] - pos[t], pos[t + 2] - pos[t]);
                const float crossLength = glm::length(cross);
                if (crossLength <= 1e-12f)
                {
                    continue;
                }
                const glm::vec3 face = cross / crossLength;
                const float facing = glm::dot(out[i], face);
                if (facing < kMinFacing)
                {
                    const glm::vec3 tangent = out[i] - face * facing;
                    const float tangentLength = glm::length(tangent);
                    out[i] = tangentLength > 1e-6f ? tangent / tangentLength * std::sqrt(1.0f - kMinFacing * kMinFacing) +
                                                         face * kMinFacing
                                                   : face;
                }
            }
        }
        return out;
    }

    std::vector<float> ScadComputeVertexOcclusion(const std::vector<glm::vec3>& pos,
                                                  const std::vector<glm::vec3>& normals,
                                                  const std::vector<ScadNormalSpan>& spans, float defaultStrength,
                                                  float defaultGround, double scale)
    {
        constexpr float kCavityDarkening = 0.40f;   // full cavity at strength 1
        constexpr float kGroundFadeMetres = 0.60f;  // wall-foot darkening fades out by this height
        constexpr float kLargeEdgeMetres = 1.50f;   // cavity fades on triangles larger than this
        // Assets::kVertexMaxOcclusion (Vertex.hpp, not included: this file also builds for the web).
        constexpr float kMaxOcclusion = 0.45f;
        const float minimum = 1.0f - kMaxOcclusion;
        const size_t triCount = pos.size() / 3;
        std::vector<float> out(pos.size(), 1.0f);

        std::vector<float> triStrength(triCount, defaultStrength);
        std::vector<float> triGround(triCount, defaultGround);
        for (const ScadNormalSpan& span : spans)
        {
            const size_t begin = std::min<size_t>(span.firstTri, triCount);
            const size_t end = std::min<size_t>(begin + span.triCount, triCount);
            for (size_t t = begin; t < end; ++t)
            {
                if (span.shading.aoStrength >= 0.0f) triStrength[t] = span.shading.aoStrength;
                if (span.shading.aoGround >= 0.0f) triGround[t] = span.shading.aoGround;
            }
        }
        const bool anyStrength = std::any_of(triStrength.begin(), triStrength.end(), [](float v) { return v > 0.0f; });
        const bool anyGround = std::any_of(triGround.begin(), triGround.end(), [](float v) { return v > 0.0f; });
        if (!anyStrength && !anyGround)
        {
            return out;
        }

        std::unordered_map<ScadPosKey, std::vector<uint32_t>, ScadPosKeyHash> adjacency;
        if (anyStrength)
        {
            adjacency.reserve(pos.size());
            for (size_t i = 0; i < pos.size(); ++i)
            {
                adjacency[MakePosKey(pos[i])].push_back(static_cast<uint32_t>(i));
            }
        }

        const float metres = static_cast<float>(scale > 0.0 ? scale : 1.0);
        const auto smoothstep = [](float e0, float e1, float x)
        {
            const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        for (size_t i = 0; i < pos.size(); ++i)
        {
            const size_t t = i / 3;
            const glm::vec3& n = normals[i];
            float darkening = 0.0f;
            if (triStrength[t] > 0.0f)
            {
                float rise = 0.0f;
                float length = 0.0f;
                int count = 0;
                for (uint32_t corner : adjacency[MakePosKey(pos[i])])
                {
                    const size_t base = corner / 3 * 3;
                    for (size_t k = base; k < base + 3; ++k)
                    {
                        if (k == corner)
                        {
                            continue;
                        }
                        const glm::vec3 d = pos[k] - pos[i];
                        const float len = glm::length(d);
                        if (len <= 1e-6f * metres)
                        {
                            continue;
                        }
                        rise += std::max(glm::dot(n, d) / len, 0.0f);
                        length += len;
                        ++count;
                    }
                }
                if (count > 0)
                {
                    const float concavity = rise / static_cast<float>(count);
                    const float meanEdge = length / static_cast<float>(count) / metres;
                    const float sizeFade = std::clamp(kLargeEdgeMetres / std::max(meanEdge, 1e-4f), 0.0f, 1.0f);
                    darkening += triStrength[t] * kCavityDarkening * smoothstep(0.05f, 0.6f, concavity) * sizeFade;
                }
            }
            if (triGround[t] > 0.0f)
            {
                const float height = std::max(pos[i].y / metres, 0.0f);
                const float wall = 1.0f - std::min(std::abs(n.y), 1.0f);
                darkening += triGround[t] * (1.0f - smoothstep(0.0f, kGroundFadeMetres, height)) * wall;
            }
            out[i] = std::clamp(1.0f - darkening, minimum, 1.0f);
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Node metadata (see FScadShared.h). The serialized form is produced by
    // FScadLoader; these readers are what gameplay uses on the other side.
    // ------------------------------------------------------------------

    // One element of a flat vector parameter, or false when the element has no
    // comma-free single-token spelling (a nested vector, a range, or a string
    // that would collide with the record's own separators).
    static bool SerializeScadMetadataElement(const Value& value, std::string& out)
    {
        switch (value.type)
        {
        case Value::Type::Number:
            out = fmt::format("{:g}", value.num);
            return true;
        case Value::Type::Bool:
            out = value.boolean ? "true" : "false";
            return true;
        case Value::Type::Str:
            if (value.str.find_first_of(",;=") != std::string::npos)
            {
                return false;
            }
            out = value.str;
            return true;
        default:
            return false;
        }
    }

    // Serializes the evaluated named parameters of a user-module call as
    // "k=v;k=v" for Node::SetMetadata. Gameplay reads authoring intent (rail
    // length, swing period, button index, which ids a door listens to, how far
    // a lift travels) out of these. Scalars keep their natural spelling; a flat
    // vector of scalars becomes its elements joined with ',' ("travel=0,0,3",
    // "on=plate_a,plate_b"), so readers split on ',' to get it back. Nested
    // vectors and ranges have no single-token spelling and are dropped. Empty
    // when the module takes no serializable parameters.
    std::string SerializeScadNodeMetadata(const SceneNode& sceneNode)
    {
        std::string out;
        for (const auto& [key, value] : sceneNode.parameters)
        {
            std::string text;
            switch (value.type)
            {
            case Value::Type::Number:
                text = fmt::format("{:g}", value.num);
                break;
            case Value::Type::Bool:
                text = value.boolean ? "true" : "false";
                break;
            case Value::Type::Str:
                // A ';' or '=' inside the string would corrupt the record; skip those.
                if (value.str.find_first_of(";=") != std::string::npos)
                {
                    continue;
                }
                text = value.str;
                break;
            case Value::Type::Vec:
            {
                bool flat = true;
                std::string element;
                for (size_t index = 0; index < value.vec.size() && flat; ++index)
                {
                    flat = SerializeScadMetadataElement(value.vec[index], element);
                    if (flat)
                    {
                        if (index > 0)
                        {
                            text.push_back(',');
                        }
                        text.append(element);
                    }
                }
                if (!flat)
                {
                    continue;
                }
                break;
            }
            default:
                continue;
            }
            if (!out.empty())
            {
                out.push_back(';');
            }
            out.append(key);
            out.push_back('=');
            out.append(text);
        }
        return out;
    }

    // `collision = "none"` on a user module opts that call's whole subtree out of
    // collision: its meshes are built raycast-invisible, which is what keeps the
    // scene from giving them an implicit static physics body and keeps them out
    // of CPU ray queries (camera booms, picking, NavGrid sampling). Pickups,
    // trigger volumes and decoration use it; anything else collides as before.
    bool ScadNodeDisablesCollision(const SceneNode& sceneNode)
    {
        for (const auto& [key, value] : sceneNode.parameters)
        {
            if (key == "collision")
            {
                return value.type == Value::Type::Str && value.str == "none";
            }
        }
        return false;
    }

    FMetadata ParseScadMetadata(std::string_view metadata)
    {
        FMetadata out;
        size_t cursor = 0;
        while (cursor < metadata.size())
        {
            const size_t end = std::min(metadata.find(';', cursor), metadata.size());
            const std::string_view entry = metadata.substr(cursor, end - cursor);
            cursor = end + 1;
            const size_t equals = entry.find('=');
            if (equals == std::string_view::npos || equals == 0)
            {
                continue;
            }
            out.emplace_back(std::string(entry.substr(0, equals)), std::string(entry.substr(equals + 1)));
        }
        return out;
    }

    namespace
    {
        const std::string* FindMetadataValue(const FMetadata& metadata, std::string_view key)
        {
            for (const auto& [entryKey, entryValue] : metadata)
            {
                if (entryKey == key)
                {
                    return &entryValue;
                }
            }
            return nullptr;
        }
    }

    double MetadataNumber(const FMetadata& metadata, std::string_view key, double fallback)
    {
        const std::string* value = FindMetadataValue(metadata, key);
        if (!value || value->empty())
        {
            return fallback;
        }
        // The whole value must be the number: a vector parameter is spelled "1,2,3", and reading
        // its first element as if it were a scalar would hide the authoring mistake.
        char* parseEnd = nullptr;
        const double parsed = std::strtod(value->c_str(), &parseEnd);
        return (parseEnd && parseEnd != value->c_str() && *parseEnd == '\0' && std::isfinite(parsed)) ? parsed
                                                                                                    : fallback;
    }

    bool MetadataBool(const FMetadata& metadata, std::string_view key, bool fallback)
    {
        const std::string* value = FindMetadataValue(metadata, key);
        if (!value)
        {
            return fallback;
        }
        if (*value == "true" || *value == "1")
        {
            return true;
        }
        if (*value == "false" || *value == "0")
        {
            return false;
        }
        return fallback;
    }

    std::string MetadataString(const FMetadata& metadata, std::string_view key, std::string_view fallback)
    {
        const std::string* value = FindMetadataValue(metadata, key);
        return value ? *value : std::string(fallback);
    }
} // namespace Assets::Scad
