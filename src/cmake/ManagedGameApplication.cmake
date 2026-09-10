include_guard(GLOBAL)

# Declares a standalone C# game application without a per-game C++ entry point. The native
# executable is still fixed at link time (especially under NativeAOT), but its manifest, managed
# project, linked-module set, and any loader registration are CMake data rather than source code.
#
# gk_add_managed_game_application(<target>
#     MANIFEST <runtime manifest path>
#     PROJECT <csproj>
#     DIR <managed publish directory>
#     MODULES <runtime modules...>
#     [LINK <engine libraries...>]
#     [ICON <icon>]
#     [REGISTER_SCAD_LOADER] [REGISTER_LDRAW_LOADER] [REGISTER_GLTF_LOADER])
function(gk_add_managed_game_application target)
    cmake_parse_arguments(ARG
        "REGISTER_SCAD_LOADER;REGISTER_LDRAW_LOADER;REGISTER_GLTF_LOADER"
        "MANIFEST;PROJECT;DIR;ICON"
        "MODULES;LINK;SOURCES"
        ${ARGN})

    foreach(required IN ITEMS MANIFEST PROJECT DIR)
        if(NOT ARG_${required})
            message(FATAL_ERROR
                "gk_add_managed_game_application(${target}) requires ${required}")
        endif()
    endforeach()

    # Modules are static-link choices. Feed the actual CMake selection to the host's manifest
    # preflight instead of keeping a second C++ list that can drift from the target.
    set(linkedModules ${ARG_MODULES} ${ARG_LINK})
    list(REMOVE_DUPLICATES linkedModules)
    string(JOIN "|" linkedModulesDefinition ${linkedModules})

    set(applicationArguments
        SOURCES
            "${GK_SOURCE_ROOT}/Application/Game/ManagedGameAppMain.cpp"
            ${ARG_SOURCES}
        MODULES ${ARG_MODULES}
        LINK ${ARG_LINK}
        DEFINES
            "GK_MANAGED_GAME_MANIFEST_PATH=\"${ARG_MANIFEST}\""
            "GK_MANAGED_GAME_LINKED_MODULES=\"${linkedModulesDefinition}\""
            "GK_MANAGED_GAME_REGISTER_SCAD_LOADER=$<BOOL:${ARG_REGISTER_SCAD_LOADER}>"
            "GK_MANAGED_GAME_REGISTER_LDRAW_LOADER=$<BOOL:${ARG_REGISTER_LDRAW_LOADER}>"
            "GK_MANAGED_GAME_REGISTER_GLTF_LOADER=$<BOOL:${ARG_REGISTER_GLTF_LOADER}>")
    if(ARG_ICON)
        list(APPEND applicationArguments ICON "${ARG_ICON}")
    endif()

    gk_add_application(${target} ${applicationArguments})
    gk_dotnet_managed_game(${target} PROJECT "${ARG_PROJECT}" DIR "${ARG_DIR}")
endfunction()
