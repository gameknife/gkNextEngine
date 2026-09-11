include_guard(GLOBAL)

# ============================================================================
# MobileApplications.cmake - the mobile application registry
# ============================================================================
# Android and iOS package exactly one application per build. Which one is a
# choice made outside CMake (Gradle names a target, `gnb ios build --app` names
# a target), so the engine only has to answer two questions about it: where
# its directory is and what identity it ships under.
#
# Both answers come from src/Application/MobileApplications.json, which
# tools/android and gnb read as well. Nothing here knows about gkNextRenderer.
#
# This file is included by both the engine project and the standalone Android
# driver project in tools/android, so it must not depend on any variable those
# two do not share.
# ============================================================================

get_filename_component(GK_MOBILE_APPLICATIONS_MANIFEST
    "${CMAKE_CURRENT_LIST_DIR}/../Application/MobileApplications.json" ABSOLUTE)
if(NOT EXISTS "${GK_MOBILE_APPLICATIONS_MANIFEST}")
    message(FATAL_ERROR "Mobile application manifest not found: ${GK_MOBILE_APPLICATIONS_MANIFEST}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GK_MOBILE_APPLICATIONS_MANIFEST}")

file(READ "${GK_MOBILE_APPLICATIONS_MANIFEST}" gkMobileManifestJson)

set(GK_MOBILE_APPLICATIONS "")
set(GK_ANDROID_APPLICATIONS "")
set(GK_IOS_APPLICATIONS "")

string(JSON gkMobileApplicationCount LENGTH "${gkMobileManifestJson}" applications)
math(EXPR gkMobileLastIndex "${gkMobileApplicationCount} - 1")
foreach(gkMobileIndex RANGE 0 ${gkMobileLastIndex})
    string(JSON gkMobileEntry GET "${gkMobileManifestJson}" applications ${gkMobileIndex})

    foreach(gkMobileField target directory label androidId iosBundleId)
        string(JSON gkMobileValue ERROR_VARIABLE gkMobileFieldError
            GET "${gkMobileEntry}" ${gkMobileField})
        if(gkMobileFieldError)
            message(FATAL_ERROR
                "MobileApplications.json entry ${gkMobileIndex} is missing '${gkMobileField}'")
        endif()
        set(gkMobile_${gkMobileField} "${gkMobileValue}")
    endforeach()

    set(gkMobileTarget "${gkMobile_target}")
    list(APPEND GK_MOBILE_APPLICATIONS "${gkMobileTarget}")
    set(GK_MOBILE_APP_${gkMobileTarget}_DIRECTORY "${gkMobile_directory}")
    set(GK_MOBILE_APP_${gkMobileTarget}_LABEL "${gkMobile_label}")
    set(GK_MOBILE_APP_${gkMobileTarget}_ANDROID_ID "${gkMobile_androidId}")
    set(GK_MOBILE_APP_${gkMobileTarget}_IOS_BUNDLE_ID "${gkMobile_iosBundleId}")

    string(JSON gkMobileDotNet ERROR_VARIABLE gkMobileDotNetError GET "${gkMobileEntry}" requiresDotNet)
    if(gkMobileDotNetError)
        set(gkMobileDotNet OFF)
    endif()
    set(GK_MOBILE_APP_${gkMobileTarget}_REQUIRES_DOTNET "${gkMobileDotNet}")

    string(JSON gkMobileIcon ERROR_VARIABLE gkMobileIconError GET "${gkMobileEntry}" icon)
    if(gkMobileIconError)
        if(EXISTS "${GK_REPO_ROOT}/assets/icons/${gkMobileTarget}.png")
            set(gkMobileIcon "${gkMobileTarget}")
        else()
            set(gkMobileIcon "gkNextEngine")
        endif()
    endif()
    set(GK_MOBILE_APP_${gkMobileTarget}_ICON "${gkMobileIcon}")

    string(JSON gkMobilePlatformCount LENGTH "${gkMobileEntry}" platforms)
    math(EXPR gkMobileLastPlatform "${gkMobilePlatformCount} - 1")
    set(gkMobilePlatforms "")
    foreach(gkMobilePlatformIndex RANGE 0 ${gkMobileLastPlatform})
        string(JSON gkMobilePlatform GET "${gkMobileEntry}" platforms ${gkMobilePlatformIndex})
        list(APPEND gkMobilePlatforms "${gkMobilePlatform}")
    endforeach()
    set(GK_MOBILE_APP_${gkMobileTarget}_PLATFORMS "${gkMobilePlatforms}")
    if("android" IN_LIST gkMobilePlatforms)
        list(APPEND GK_ANDROID_APPLICATIONS "${gkMobileTarget}")
    endif()
    if("ios" IN_LIST gkMobilePlatforms)
        list(APPEND GK_IOS_APPLICATIONS "${gkMobileTarget}")
    endif()
endforeach()

# Resolves a possibly-empty, possibly-differently-cased application name against the registry for
# one platform ("android" or "ios"). An empty name selects the first application the manifest lists
# for that platform, which is what makes gkNextRenderer the default without naming it here.
#
# A non-registry name can be a C# project under projects/<Game>/Scripts/. Those projects get a
# generated NativeAOT application with a conventional identity; no MobileApplications.json entry
# or handwritten C++ target is needed. The registry remains the only authority for native C++
# applications, whose identity and source directory cannot be inferred safely.
function(gk_resolve_mobile_application platform requestedName outputVariable)
    if(platform STREQUAL "android")
        set(candidates ${GK_ANDROID_APPLICATIONS})
    elseif(platform STREQUAL "ios")
        set(candidates ${GK_IOS_APPLICATIONS})
    else()
        message(FATAL_ERROR "Unknown mobile platform '${platform}'")
    endif()

    if(NOT requestedName)
        list(GET candidates 0 resolved)
        set(${outputVariable} "${resolved}" PARENT_SCOPE)
        return()
    endif()

    foreach(candidate IN LISTS candidates)
        string(TOLOWER "${candidate}" candidateLower)
        string(TOLOWER "${requestedName}" requestedLower)
        if(candidateLower STREQUAL requestedLower)
            set(${outputVariable} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    file(GLOB_RECURSE gkManagedProjectFiles CONFIGURE_DEPENDS
        "${GK_REPO_ROOT}/projects/*/Scripts/*.csproj")
    set(gkManagedMatch "")
    foreach(gkManagedProject IN LISTS gkManagedProjectFiles)
        get_filename_component(gkManagedTarget "${gkManagedProject}" NAME_WE)
        string(TOLOWER "${gkManagedTarget}" gkManagedTargetLower)
        string(TOLOWER "${requestedName}" requestedLower)
        if(NOT gkManagedTargetLower STREQUAL requestedLower)
            continue()
        endif()

        if(gkManagedMatch)
            message(FATAL_ERROR
                "C# project name '${requestedName}' is ambiguous: '${gkManagedMatch}' and "
                "'${gkManagedProject}'")
        endif()
        set(gkManagedMatch "${gkManagedProject}")
    endforeach()

    if(gkManagedMatch)
        set(gkManagedProject "${gkManagedMatch}")
        get_filename_component(gkManagedTarget "${gkManagedProject}" NAME_WE)

        get_filename_component(gkManagedScriptsDirectory "${gkManagedProject}" DIRECTORY)
        get_filename_component(gkManagedProjectDirectory "${gkManagedScriptsDirectory}" DIRECTORY)
        get_filename_component(gkManagedProjectDirectoryName "${gkManagedProjectDirectory}" NAME)
        string(TOLOWER "${gkManagedProjectDirectoryName}" gkManagedGameId)
        string(TOLOWER "${gkManagedTarget}" gkManagedTargetLower)
        string(REGEX REPLACE "[^a-z0-9]" "" gkManagedIdentitySuffix "${gkManagedTargetLower}")
        if(gkManagedIdentitySuffix STREQUAL "")
            message(FATAL_ERROR "C# project '${gkManagedProject}' has no usable mobile identity")
        endif()

        set(GK_MOBILE_APP_${gkManagedTarget}_DIRECTORY "")
        set(GK_MOBILE_APP_${gkManagedTarget}_LABEL "${gkManagedTarget}")
        set(GK_MOBILE_APP_${gkManagedTarget}_ANDROID_ID "com.gknext.${gkManagedIdentitySuffix}")
        set(GK_MOBILE_APP_${gkManagedTarget}_IOS_BUNDLE_ID "gknext.${gkManagedIdentitySuffix}")
        set(GK_MOBILE_APP_${gkManagedTarget}_ICON "gkNextEngine")
        set(GK_MOBILE_APP_${gkManagedTarget}_REQUIRES_DOTNET ON)
        set(GK_MOBILE_APP_${gkManagedTarget}_AUTO_MANAGED ON)
        set(GK_MOBILE_APP_${gkManagedTarget}_MANAGED_PROJECT "${gkManagedProject}")
        set(GK_MOBILE_APP_${gkManagedTarget}_MANAGED_GAME_ID "${gkManagedGameId}")

        foreach(gkManagedField IN ITEMS
            DIRECTORY LABEL ANDROID_ID IOS_BUNDLE_ID ICON REQUIRES_DOTNET AUTO_MANAGED
            MANAGED_PROJECT MANAGED_GAME_ID)
            set(GK_MOBILE_APP_${gkManagedTarget}_${gkManagedField}
                "${GK_MOBILE_APP_${gkManagedTarget}_${gkManagedField}}" PARENT_SCOPE)
        endforeach()
        set(${outputVariable} "${gkManagedTarget}" PARENT_SCOPE)
        return()
    endif()

    string(REPLACE ";" ", " availableText "${candidates}")
    message(FATAL_ERROR
        "'${requestedName}' is not a ${platform} application.\n"
        "Available: ${availableText}\n"
        "Use a C# project name from projects/*/Scripts/, or add a native application to "
        "${GK_MOBILE_APPLICATIONS_MANIFEST}.")
endfunction()
