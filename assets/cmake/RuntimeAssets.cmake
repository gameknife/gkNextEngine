set(output_base_dir ${CMAKE_CURRENT_BINARY_DIR})
if(ANDROID)
    if(NOT GK_ANDROID_ASSET_OUTPUT_DIR)
        message(FATAL_ERROR
            "GK_ANDROID_ASSET_OUTPUT_DIR is required for Android builds. "
            "Configure Android through tools/android so assets stay in the build tree.")
    endif()
    get_filename_component(output_base_dir "${GK_ANDROID_ASSET_OUTPUT_DIR}" ABSOLUTE)
elseif(IOS)
    set(output_base_dir ${CMAKE_CURRENT_BINARY_DIR}/assets/)
endif()

set(ASSET_DIRS
    anims
    brand
    configs
    fonts
    # Application and game icons
    icons
    # Generated city tiles (`gnb geo`). Gitignored and normally shipped inside
    # assets/paks/geo.pak, so the directory is often absent — the EXISTS guard
    # below skips it, and the pak covers the runtime either way.
    geo
    legos
    locale
    models
    omr
    paks
    remote
    rmlui_demo
    scad
    sog
    scripts
    sfx
    sounds
    # Game project templates, read by the "new project" dialog in gkNextLauncher and gkNextEditor.
    templates
    textures
)

# Game projects live outside this directory, in <repo>/projects/<Game>/, each one holding its
# manifest, its Content/ and its C# Scripts/. At runtime they are one more subtree of the asset
# namespace — assets/projects/<Game>/ — so paks, the asset trace, the Android APK and the iOS bundle
# all carry them without knowing they exist. Only the manifest and Content/ are copied: Scripts/
# reaches the runtime as the published assembly in <bin>/csharp, never as source.
set(game_projects_src_dir "${CMAKE_SOURCE_DIR}/projects")
set(game_projects_out_dir "${output_base_dir}/projects")

# Android packages the generated asset tree directly through Gradle. Keep that
# tree exact across CMake regenerations so removed source assets cannot survive
# in a later APK. The stamps are removed together with their destinations so the
# copy commands below repopulate every directory after the cleanup.
if(ANDROID)
    foreach(dir IN LISTS ASSET_DIRS)
        file(REMOVE_RECURSE "${output_base_dir}/${dir}")
        file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/${dir}.stamp")
    endforeach()
    file(REMOVE_RECURSE "${game_projects_out_dir}")
    file(GLOB stale_project_stamps "${CMAKE_CURRENT_BINARY_DIR}/project-*.stamp")
    if(stale_project_stamps)
        file(REMOVE ${stale_project_stamps})
    endif()
endif()

set(all_asset_files "")
set(all_asset_stamps "")

foreach(dir IN LISTS ASSET_DIRS)
    set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
    if(NOT EXISTS "${src_dir}")
        message(STATUS "Asset folder '${dir}' not found at ${src_dir}; skipping copy. Run 'gnb paks fetch' from the repository root (then re-run CMake configure) if you need optional assets.")
        continue()
    endif()

    file(GLOB_RECURSE ${dir}_files CONFIGURE_DEPENDS "${src_dir}/*")
    list(APPEND all_asset_files ${${dir}_files})

    set(${dir}_stamp "${CMAKE_CURRENT_BINARY_DIR}/${dir}.stamp")
    list(APPEND all_asset_stamps ${${dir}_stamp})
    add_custom_command(
        OUTPUT ${${dir}_stamp}
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${src_dir}"
            "${output_base_dir}/${dir}"
        COMMAND ${CMAKE_COMMAND} -E touch ${${dir}_stamp}
        DEPENDS ${${dir}_files}
        COMMENT "Copying ${dir}..."
    )
endforeach()

# One copy rule per project, so editing one game's content does not recopy every other game's.
# The directory listing is CONFIGURE_DEPENDS: a project created after configure (the launcher's New
# Project writes one) is picked up by the next build instead of waiting for a manual reconfigure.
# Directories starting with '_' are scratch — `gnb dotnet templates` instantiates its throwaway
# projects there and deletes them again.
file(GLOB game_project_dirs CONFIGURE_DEPENDS LIST_DIRECTORIES true "${game_projects_src_dir}/*")
foreach(project_dir IN LISTS game_project_dirs)
    get_filename_component(project_name "${project_dir}" NAME)
    if(NOT IS_DIRECTORY "${project_dir}" OR project_name MATCHES "^_")
        continue()
    endif()

    file(GLOB project_manifests CONFIGURE_DEPENDS "${project_dir}/*.game.json")
    if(NOT project_manifests)
        continue()
    endif()
    file(GLOB_RECURSE project_content CONFIGURE_DEPENDS "${project_dir}/Content/*")
    list(APPEND all_asset_files ${project_manifests} ${project_content})

    set(project_out_dir "${game_projects_out_dir}/${project_name}")
    set(project_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory "${project_out_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${project_manifests} "${project_out_dir}")
    if(IS_DIRECTORY "${project_dir}/Content")
        list(APPEND project_commands
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${project_dir}/Content" "${project_out_dir}/Content")
    endif()

    set(project_stamp "${CMAKE_CURRENT_BINARY_DIR}/project-${project_name}.stamp")
    list(APPEND all_asset_stamps ${project_stamp})
    add_custom_command(
        OUTPUT ${project_stamp}
        ${project_commands}
        COMMAND ${CMAKE_COMMAND} -E touch ${project_stamp}
        DEPENDS ${project_manifests} ${project_content}
        COMMENT "Copying game project ${project_name}..."
        VERBATIM
    )
endforeach()

if(NOT ANDROID AND NOT IOS AND Vulkan_SLANGC)
    get_filename_component(slangc_tool_dir "${Vulkan_SLANGC}" DIRECTORY)
    file(GLOB slang_tool_files CONFIGURE_DEPENDS
        "${slangc_tool_dir}/slangc*"
        "${slangc_tool_dir}/slang*.dll"
        "${slangc_tool_dir}/libslang*"
        "${slangc_tool_dir}/../lib/libslang*"
    )
    if(slang_tool_files)
        set(slang_tool_stamp "${CMAKE_CURRENT_BINARY_DIR}/slang-tool.stamp")
        list(APPEND all_asset_files ${slang_tool_files})
        list(APPEND all_asset_stamps ${slang_tool_stamp})
        add_custom_command(
            OUTPUT ${slang_tool_stamp}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${output_base_dir}/../tools/slang"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${slang_tool_files}
                "${output_base_dir}/../tools/slang"
            COMMAND ${CMAKE_COMMAND} -E touch ${slang_tool_stamp}
            DEPENDS ${slang_tool_files}
            COMMENT "Copying bundled Slang compiler..."
            VERBATIM
        )
    endif()
endif()

source_group("Assets" FILES ${all_asset_files})
