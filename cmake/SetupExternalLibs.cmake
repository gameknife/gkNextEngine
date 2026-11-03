# External downloads are prepared by `gnb setup`. CMake only validates paths.

# --- NVIDIA Streamline ---
# Pinned rather than globbed: an older SDK left under external/ must not be picked up silently.
# Keep in sync with [external.streamline].version in gnb.toml.
set(GK_STREAMLINE_VERSION "2.14.16")
if(WITH_STREAMLINE)
    set(STREAMLINE_ROOT "${CMAKE_SOURCE_DIR}/external/streamline-${GK_STREAMLINE_VERSION}")
    if(NOT EXISTS "${STREAMLINE_ROOT}/include/sl.h")
        message(FATAL_ERROR "Streamline SDK v${GK_STREAMLINE_VERSION} missing under external/. Run `gnb deps fetch streamline` (or `gnb setup`) first.")
    endif()
    set(STREAMLINE_INCLUDE_DIR "${STREAMLINE_ROOT}/include")
    set(STREAMLINE_LIB_DIR "${STREAMLINE_ROOT}/lib/x64")
    set(STREAMLINE_BIN_DIR "${STREAMLINE_ROOT}/bin/x64")
    message(STATUS "Streamline Root: ${STREAMLINE_ROOT}")
endif()

# --- AMD FidelityFX SDK (FSR 3.1 Vulkan) ---
if(WITH_FIDELITYFX)
    set(FIDELITYFX_SDK_CONTAINER "${CMAKE_SOURCE_DIR}/external/fidelityfx-sdk-1.1.4")
    set(FIDELITYFX_SDK_ROOT "${FIDELITYFX_SDK_CONTAINER}/FidelityFX-SDK-1.1.4")
    if(NOT EXISTS "${FIDELITYFX_SDK_ROOT}/ffx-api/include/ffx_api/ffx_api.h" OR
       NOT EXISTS "${FIDELITYFX_SDK_ROOT}/PrebuiltSignedDLL/amd_fidelityfx_vk.lib")
        message(FATAL_ERROR "FidelityFX SDK v1.1.4 missing under external/. Run `gnb setup` first.")
    endif()
    set(FIDELITYFX_INCLUDE_DIR "${FIDELITYFX_SDK_ROOT}/ffx-api/include")
    set(FIDELITYFX_LIB "${FIDELITYFX_SDK_ROOT}/PrebuiltSignedDLL/amd_fidelityfx_vk.lib")
    set(FIDELITYFX_DLL "${FIDELITYFX_SDK_ROOT}/PrebuiltSignedDLL/amd_fidelityfx_vk.dll")
    message(STATUS "FidelityFX SDK Root: ${FIDELITYFX_SDK_ROOT}")
endif()

