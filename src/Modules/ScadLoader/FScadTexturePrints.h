#pragma once

// ============================================================================
// FScadTexturePrints.h - Named gk_texture prints, defined in SCAD.
//
// gk_texture("plaster") does not name C++ code: it names the function
// tex_plaster() in assets/scad/lib/gk_texgraph.scad, which returns a texture
// graph (TextureGraph.hpp). The library is read once, each print evaluated
// and compiled on first use and cached; editing the library file reloads it.
// Adding `function tex_foo() = tx_surface(...);` there adds gk_texture("foo")
// -- for SCAD scenes, rigs and the terrain prints alike -- with no engine change.
// ============================================================================

#include "Engine/Assets/Loaders/ProceduralTexture.hpp"
#include "Modules/ScadLoader/FScadTypes.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Assets::Scad
{
    // The library every named print comes from.
    inline constexpr const char* kTexturePrintLibrary = "assets/scad/lib/gk_texgraph.scad";

    // A SCAD value as a texture expression (numbers, strings, lists; bool -> 0/1).
    FTexExpr ToTexExpr(const Value& value);

    // The compiled graph of tex_<name>(); null with `error` set when the print
    // is unknown or its graph does not compile.
    std::shared_ptr<const FTextureGraph> ResolveTexturePrint(std::string_view name, std::string& error);

    // Every print the library defines (the <name> of each tex_<name>), sorted.
    std::vector<std::string> TexturePrintNames();

    // A ready texture for a named print (pattern = name, graph resolved), or
    // null with `error` set.
    std::shared_ptr<FProceduralTexture> MakeTexturePrint(std::string_view name, std::string& error);
}
