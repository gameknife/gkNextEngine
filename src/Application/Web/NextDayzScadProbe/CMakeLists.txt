cmake_minimum_required(VERSION 3.24)
project(NextDayzScadProbe LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Build this probe with the Emscripten toolchain.")
endif()

include(FetchContent)
find_package(Git REQUIRED)
get_filename_component(repo "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(ND_SCAD_HEADERS "" CACHE PATH "Native vcpkg include directory")
foreach(header glm/glm.hpp fmt/format.h spdlog/spdlog.h xxhash.h mapbox/earcut.hpp nlohmann/json.hpp)
    if(NOT EXISTS "${ND_SCAD_HEADERS}/${header}")
        message(FATAL_ERROR "Missing ${header} in ND_SCAD_HEADERS. Build NextDayz first.")
    endif()
endforeach()

set(BUILD_SHARED_LIBS OFF)
set(MANIFOLD_PAR OFF)
set(MANIFOLD_TEST OFF)
set(MANIFOLD_CROSS_SECTION OFF)
set(MANIFOLD_CBIND OFF)
set(MANIFOLD_JSBIND OFF)
set(MANIFOLD_STRICT OFF)

set(manifoldURL https://codeload.github.com/elalish/manifold/tar.gz/refs/tags/v3.2.1)
set(freetypeURL https://codeload.github.com/freetype/freetype/tar.gz/refs/tags/VER-2-14-3)
set(meshoptimizerURL https://codeload.github.com/zeux/meshoptimizer/tar.gz/refs/tags/v1.1)
if(EXISTS "${repo}/.vcpkg/downloads/elalish-manifold-v3.2.1.tar.gz")
    set(manifoldURL "${repo}/.vcpkg/downloads/elalish-manifold-v3.2.1.tar.gz")
endif()
if(EXISTS "${repo}/.vcpkg/downloads/freetype-freetype-VER-2-14-3.tar.gz")
    set(freetypeURL "${repo}/.vcpkg/downloads/freetype-freetype-VER-2-14-3.tar.gz")
endif()
if(EXISTS "${repo}/.vcpkg/downloads/zeux-meshoptimizer-v1.1.tar.gz")
    set(meshoptimizerURL "${repo}/.vcpkg/downloads/zeux-meshoptimizer-v1.1.tar.gz")
endif()
set(patches "${repo}/cmake/vcpkg-overlays/manifold")
FetchContent_Declare(manifold
    URL "${manifoldURL}"
    URL_HASH SHA256=c2fddb0f4b2289caff660b29677883f0324415a9901f8f2aed4c83851f994c13
    PATCH_COMMAND "${GIT_EXECUTABLE}" apply "${patches}/fix-size_t.patch" "${patches}/fix-serial-unordered-map.patch")
foreach(dep ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${dep} ON CACHE BOOL "Skip optional FreeType dependency")
endforeach()
FetchContent_Declare(freetype
    URL "${freetypeURL}"
    URL_HASH SHA256=dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261)
FetchContent_Declare(meshoptimizer
    URL "${meshoptimizerURL}"
    URL_HASH SHA256=b787011f81b4b3069c2f9065b7c191efdd4189a49be32ba5282dd5579f05261a)
FetchContent_MakeAvailable(manifold freetype meshoptimizer)

set(scad "${repo}/src/Modules/ScadLoader")
set(texture "${repo}/src/Engine/Assets/Loaders")
add_executable(ndscad
    NextDayzScadProbe.cpp
    NextDayzScadLevel.cpp
    "${repo}/src/Application/Game/NextDayz/World/WorldAnchorData.cpp"
    "${scad}/FScadLexer.cpp"
    "${scad}/FScadParser.cpp"
    "${scad}/FScadShared.cpp"
    "${scad}/FScadEvaluator.cpp"
    "${scad}/FScadEvaluator.Expr.cpp"
    "${scad}/FScadEvaluator.Geometry.cpp"
    "${scad}/FScadCsg.cpp"
    "${scad}/FScadGeometry.cpp"
    "${scad}/FScadTerrain.cpp"
    "${scad}/FScadText.cpp"
    "${scad}/FScadTess.cpp"
    "${scad}/FScadTexturePrints.cpp"
    "${texture}/ProceduralTextureBake.cpp"
    "${texture}/TextureGraph.cpp")
target_include_directories(ndscad PRIVATE "${repo}/src" "${repo}/src/Engine")
target_include_directories(ndscad SYSTEM PRIVATE "${ND_SCAD_HEADERS}")
target_compile_features(ndscad PRIVATE cxx_std_20)
target_compile_definitions(ndscad PRIVATE
    FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL XXH_INLINE_ALL
    GK_WITH_MANIFOLD=1 GK_WITH_FREETYPE=1 GK_WITH_EARCUT=1)
target_compile_options(ndscad PRIVATE -fexceptions)
target_link_libraries(ndscad PRIVATE manifold freetype meshoptimizer)
target_link_options(ndscad PRIVATE
    -fexceptions
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sENVIRONMENT=worker,node
    -sFORCE_FILESYSTEM=1
    -sSTACK_SIZE=2MB
    -sALLOW_MEMORY_GROWTH=1
    "-sEXPORTED_FUNCTIONS=['_free']"
    "-sEXPORTED_RUNTIME_METHODS=['FS','HEAPF32','HEAPU8','UTF8ToString','stringToNewUTF8']")
set_target_properties(ndscad PROPERTIES SUFFIX ".mjs" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../public")
