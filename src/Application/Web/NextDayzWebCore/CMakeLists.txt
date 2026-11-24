cmake_minimum_required(VERSION 3.25)
project(NextDayzWebCore LANGUAGES CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Build NextDayzWebCore with the Emscripten toolchain")
endif()
if(NOT EXISTS "${ND_CORE_HEADERS}/glm/glm.hpp" OR NOT EXISTS "${ND_CORE_HEADERS}/fmt/format.h" OR
   NOT EXISTS "${ND_CORE_HEADERS}/nlohmann/json.hpp" OR NOT EXISTS "${ND_CORE_HEADERS}/spdlog/spdlog.h")
    message(FATAL_ERROR "ND_CORE_HEADERS must point to the native vcpkg include directory")
endif()

add_subdirectory(../../Game/NextDayz/Core NextDayzCore)
target_compile_options(NextDayzCore PRIVATE -fexceptions)

set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "")
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "")
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "")
set(USE_WASM_SIMD ON CACHE BOOL "")
if(ND_JOLT_SOURCE)
    add_subdirectory("${ND_JOLT_SOURCE}/Build" jolt EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JoltPhysics
        URL https://codeload.github.com/jrouwe/JoltPhysics/tar.gz/refs/tags/v5.4.0
        URL_HASH SHA256=1d2fdc33ef9a2da69efd704adc0a97da7c6ed698c2bded04b0c36f31207b0829
        SOURCE_SUBDIR Build
    )
    FetchContent_MakeAvailable(JoltPhysics)
endif()

# ndcore is the release kernel. ndcore-review is the same kernel plus the review
# shortcuts (teleport, a crowd of infected) for gnb web dev and the Playwright scripts.
function(nd_add_kernel name)
    add_executable(${name} WebCore.cpp WebPhysics.cpp WebCharacters.cpp)
    target_sources(${name} PRIVATE ../../../Modules/NextPhysics/JoltCharacter.cpp)
    target_link_libraries(${name} PRIVATE NextDayzCore Jolt)
    target_compile_options(${name} PRIVATE -fexceptions)
    target_link_options(${name} PRIVATE
        --no-entry
        -fexceptions
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sENVIRONMENT=web,node"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
        "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','UTF8ToString']"
    )
    set_target_properties(${name} PROPERTIES SUFFIX ".mjs")
endfunction()
nd_add_kernel(ndcore)
nd_add_kernel(ndcore-review)
target_compile_definitions(ndcore-review PRIVATE ND_REVIEW_COMMANDS=1)

add_executable(NextDayzProtocolProbe ProtocolProbe.cpp)
target_link_libraries(NextDayzProtocolProbe PRIVATE NextDayzCore)
target_link_options(NextDayzProtocolProbe PRIVATE -fexceptions "-sENVIRONMENT=node")
set_target_properties(NextDayzProtocolProbe PROPERTIES SUFFIX ".cjs")

add_executable(NextDayzCoreRules CoreRulesProbe.cpp)
target_link_libraries(NextDayzCoreRules PRIVATE NextDayzCore)
target_link_options(NextDayzCoreRules PRIVATE -fexceptions "-sENVIRONMENT=node")
set_target_properties(NextDayzCoreRules PROPERTIES SUFFIX ".cjs")

add_executable(NextDayzClientReplay ClientReplayProbe.cpp)
target_link_libraries(NextDayzClientReplay PRIVATE NextDayzCore)
target_link_options(NextDayzClientReplay PRIVATE -fexceptions "-sENVIRONMENT=node")
set_target_properties(NextDayzClientReplay PROPERTIES SUFFIX ".cjs")


add_executable(NextDayzRigPoseProbe RigPoseProbe.cpp)
target_link_libraries(NextDayzRigPoseProbe PRIVATE NextDayzCore)
target_compile_options(NextDayzRigPoseProbe PRIVATE -fexceptions)
target_link_options(NextDayzRigPoseProbe PRIVATE -fexceptions "-sENVIRONMENT=node" "-sNODERAWFS=1")
set_target_properties(NextDayzRigPoseProbe PROPERTIES SUFFIX ".cjs")
