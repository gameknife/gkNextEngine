#pragma once

// ============================================================================
// FScadShared.h - Internal helpers shared between the SCAD scene loader
// (FScadLoader) and the SCAD rig loader (FScadRigLoader): use/include closure
// resolution and SCAD (Z-up) -> engine (Y-up) conversions.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Modules/ScadLoader/FScadTypes.h"

namespace Assets::Scad
{
    // Parsed use/include closure of a .scad program: merged definition tables
    // plus the executable top level of the main file (and its includes).
    struct ScadProgram
    {
        std::unordered_map<std::string, StmtPtr> modules;
        std::unordered_map<std::string, StmtPtr> functions;
        Scope mainTopLevel;
    };

    // Resolves filename (platform-relative or absolute), reads the file and its
    // use/include closure, and lexes/parses everything into outProgram.
    //
    // outFiles, when given, receives every file the closure touched as an absolute
    // path, main file first -- including the one that failed to read or parse, so a
    // live editor watching these paths sees the fix for a broken include too.
    bool LoadScadProgram(const std::string& filename, ScadProgram& outProgram, std::string& outError,
                         std::vector<std::string>* outFiles = nullptr);

    // Offline tools can opt into the source checkout instead of the engine's
    // staged assets. Configure once at startup, before any evaluator starts.
    void SetLooseSourceRoot(std::filesystem::path root);
    bool ScadHasPackage();
    std::filesystem::path ResolveScadLoosePath(const std::filesystem::path& path);
    std::vector<std::string> LooseSourceSidecars();

    // Reads a runtime-root-relative asset as raw bytes through the package file
    // system (with a loose-file fallback). Same resolution the use/include
    // closure uses, so binary side-car assets referenced from .scad (e.g. the
    // terrain .hmap) work identically in packed and loose builds.
    bool ScadReadAsset(const std::string& path, std::vector<uint8_t>& out);

    // Converts a SCAD (Z-up) point to engine (Y-up) space with uniform scale.
    glm::vec3 ScadToWorldPos(const glm::dvec3& p, double scale);

    // Basis matrix for the Z-up -> Y-up change (pure rotation times scale).
    glm::dmat4 ScadToWorldBasis(double scale);

    // Stable per-load identity for a concrete SCAD call site. It is based on
    // the call's source byte offset, so same-named modules on one line remain
    // distinct placement targets.
    std::string ScadPlacementTag(size_t sourceOffset);

    // Conjugates a SCAD-local transform into engine space and decomposes it.
    void ScadLocalToEngineTRS(
        const glm::dmat4& scadLocal,
        double scale,
        glm::vec3& outTranslation,
        glm::quat& outRotation,
        glm::vec3& outScale);

    // OpenSCAD rotate([x,y,z]) semantics: Rz * Ry * Rx, degrees.
    glm::dmat4 ScadRotateXYZ(const glm::dvec3& degrees);

    // Per-corner smoothed normals for a triangle soup (see FScadLoader).
    std::vector<glm::vec3> ScadComputeSmoothNormals(const std::vector<glm::vec3>& pos, float angleThresholdDeg);

    // Normals for a mesh bucket whose triangles may carry gk_crease /
    // gk_puff_normals spans. pos is the bucket's soup already converted with
    // ScadToWorldPos(p, scale) (engine space, +Y up); spans index its triangles
    // and keep their puff centre in SCAD space. Triangles of different crease
    // angles smooth separately; authored normals (SCAD space, one per corner,
    // zero = none; gk_round_box / gk_sweep know their exact surface) replace
    // the smoothed ones; puff then bends each span's normals toward its
    // centre. With no spans and no authored normals this is
    // ScadComputeSmoothNormals(pos, defaultAngle).
    std::vector<glm::vec3> ScadComputeShadedNormals(const std::vector<glm::vec3>& pos, float defaultAngleDeg,
                                                    const std::vector<ScadNormalSpan>& spans,
                                                    const std::vector<glm::dvec3>& authored, double scale);

    // Baked ambient occlusion per corner, in [1 - Assets::kVertexMaxOcclusion, 1]
    // (1 = open), for Assets::EncodeTangentW. pos / normals as for the normals
    // above (engine space, +Y up, `scale` units per metre).
    //  - cavity: neighbours rising above a corner's tangent plane (an inside
    //    corner, a fold, a pocket seam) darken it; big triangles are attenuated,
    //    since a vertex value would smear across the whole face;
    //  - ground: walls darken toward local height 0 (the foot of a building or
    //    prop in its own frame); floors and roofs are left alone.
    // gk_ao spans override the defaults per triangle; 0 disables a term.
    std::vector<float> ScadComputeVertexOcclusion(const std::vector<glm::vec3>& pos,
                                                  const std::vector<glm::vec3>& normals,
                                                  const std::vector<ScadNormalSpan>& spans, float defaultStrength,
                                                  float defaultGround, double scale);

    // ------------------------------------------------------------------
    struct SceneNode;
    std::string SerializeScadNodeMetadata(const SceneNode& sceneNode);
    bool ScadNodeDisablesCollision(const SceneNode& sceneNode);

    // Node metadata: the evaluated named parameters of a user-module call,
    // serialized as "k=v;k=v" into Assets::Node::SetMetadata by the scene
    // loader. Numbers use %g, bools "true"/"false", strings are copied
    // verbatim; vectors and ranges are skipped (gameplay reads scalars).
    // ------------------------------------------------------------------
    using FMetadata = std::vector<std::pair<std::string, std::string>>;

    FMetadata ParseScadMetadata(std::string_view metadata);
    // Reads one key out of an already parsed metadata table.
    double MetadataNumber(const FMetadata& metadata, std::string_view key, double fallback);
    bool MetadataBool(const FMetadata& metadata, std::string_view key, bool fallback);
    std::string MetadataString(const FMetadata& metadata, std::string_view key, std::string_view fallback);
} // namespace Assets::Scad
