#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/ScadLoader/FScadCsg.h"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadParser.h"
#include "Modules/ScadLoader/FScadShared.h"
#include "Modules/ScadLoader/FScadTerrain.h"
#include "Modules/ScadLoader/FScadTess.h"
#include "Modules/ScadLoader/FScadText.h"

#include <emscripten/emscripten.h>
#include <nlohmann/json.hpp>

namespace
{
    using namespace Assets::Scad;

    constexpr double gateX = -60.0;
    constexpr double gateY = -260.0;
    constexpr size_t floatsPerVertex = 9;
    std::vector<float> vertices;
    std::string report;

    // Published training ground's main gate. Evaluate the same terrain and the
    // first iteration of the original site loop, with its original arguments.
    bool SelectGate(const ScadProgram& program, Scope& scope, std::string& error)
    {
        Scope firstIteration;
        std::vector<Token> tokens;
        if (!ScadLexer::Tokenize("for(i=[0:0]) nd_pcg_site();", tokens, error) ||
            !ScadParser::Parse(tokens, firstIteration, error))
        {
            return false;
        }

        bool foundTerrain = false;
        bool foundGate = false;
        for (const auto& stmt : program.mainTopLevel)
        {
            if (stmt->kind == StmtKind::Assign)
            {
                scope.push_back(stmt);
            }
            else if (stmt->kind == StmtKind::Instance && stmt->name == "gk_terrain")
            {
                scope.push_back(stmt);
                foundTerrain = true;
            }
            else if (stmt->kind == StmtKind::Instance && stmt->name == "for" &&
                     stmt->children.size() == 1 && stmt->children.front()->name == "nd_pcg_site")
            {
                firstIteration.front()->children = stmt->children;
                scope.push_back(firstIteration.front());
                foundGate = true;
            }
        }
        if (!foundGate || !foundTerrain)
        {
            error = "The published training ground must contain its terrain and site loop.";
            return false;
        }
        return true;
    }

    bool IsNearGate(const std::array<glm::dvec3, 3>& triangle)
    {
        const auto& [a, b, c] = triangle;
        return std::max({a.x, b.x, c.x}) >= gateX - 48 && std::min({a.x, b.x, c.x}) <= gateX + 48 &&
               std::max({a.y, b.y, c.y}) >= gateY - 50 && std::min({a.y, b.y, c.y}) <= gateY + 50;
    }

    void AppendMesh(const SceneMeshBucket& mesh, const glm::dmat4& transform, std::vector<float>& out)
    {
        std::vector<glm::vec3> positions;
        for (size_t i = 0; i + 2 < mesh.tris.size(); i += 3)
        {
            std::array<glm::dvec3, 3> triangle;
            for (size_t corner = 0; corner < triangle.size(); ++corner)
            {
                triangle[corner] = glm::dvec3(transform * glm::dvec4(mesh.tris[i + corner], 1.0));
            }
            if (IsNearGate(triangle))
            {
                for (const auto& p : triangle)
                {
                    positions.push_back(ScadToWorldPos(p, 1.0));
                }
            }
        }
        if (positions.empty())
        {
            return;
        }
        const auto normals = ScadComputeSmoothNormals(positions, mesh.faceted ? 0.0f : 45.0f);
        for (size_t i = 0; i < positions.size(); ++i)
        {
            const auto& p = positions[i];
            const auto& n = normals[i];
            out.insert(out.end(), {p.x, p.y, p.z, n.x, n.y, n.z, mesh.color.r, mesh.color.g, mesh.color.b});
        }
    }

    void AppendNode(const SceneNode& node, const glm::dmat4& parent, std::vector<float>& decals)
    {
        const glm::dmat4 transform = parent * node.localTransform;
        for (const auto& mesh : node.meshes)
        {
            AppendMesh(mesh, transform, mesh.terrainDecal ? decals : vertices);
        }
        for (const auto& child : node.children)
        {
            AppendNode(child, transform, decals);
        }
    }

    bool GenerateGate(const std::string& path, std::string& error)
    {
        if (!ScadCsg::BackendAvailable() || !ScadText::Available() || !ScadTess::Available())
        {
            error = "The probe requires Manifold, FreeType and Earcut.";
            return false;
        }
        ScadProgram program;
        Scope scope;
        if (!LoadScadProgram(path, program, error) || !SelectGate(program, scope, error))
        {
            return false;
        }

        Assets::ScadLoadOptions options;
        options.parallelTopLevel = false;
        SceneEvalResult result;
        if (!ScadEvaluator::EvaluateScene(scope, program.modules, program.functions, options, result, error))
        {
            return false;
        }
        if (result.terrains.size() != 1)
        {
            error = "Expected one evaluated training ground terrain.";
            return false;
        }
        std::vector<float> decals;
        for (const auto& node : result.roots)
        {
            AppendNode(node, glm::dmat4(1.0), decals);
        }
        if (vertices.empty())
        {
            error = "No geometry intersects the training ground gate.";
            return false;
        }

        const size_t decalStart = vertices.size() / floatsPerVertex;
        vertices.insert(vertices.end(), decals.begin(), decals.end());
        const auto gate = ScadToWorldPos({gateX, gateY, result.terrains.front().data->HeightAt(gateX, gateY)}, 1.0);
        report = nlohmann::json{
            {"triangleCount", vertices.size() / (floatsPerVertex * 3)},
            {"evaluatedTriangles", result.triangleCount},
            {"terrainCount", result.terrains.size()},
            {"warningCount", result.warningCount},
            {"warnings", result.warnings},
            {"decalStart", decalStart},
            {"gate", {gate.x, gate.y, gate.z}}
        }.dump();
        return true;
    }
}

extern "C"
{
    EMSCRIPTEN_KEEPALIVE int ScadGenerate(const char* path)
    {
        vertices.clear();
        try
        {
            std::string error;
            if (GenerateGate(path, error))
            {
                return 1;
            }
            report = error;
        }
        catch (const std::exception& exception)
        {
            report = exception.what();
        }
        vertices.clear();
        return 0;
    }

    EMSCRIPTEN_KEEPALIVE const float* ScadVertices()
    {
        return vertices.data();
    }

    EMSCRIPTEN_KEEPALIVE size_t ScadFloatCount()
    {
        return vertices.size();
    }

    EMSCRIPTEN_KEEPALIVE const char* ScadReport()
    {
        return report.c_str();
    }
}
