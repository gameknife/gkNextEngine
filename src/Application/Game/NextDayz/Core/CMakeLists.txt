set(nextdayz "${CMAKE_CURRENT_LIST_DIR}/..")
add_library(NextDayzCore STATIC
    CharacterPoseGraph.cpp
    SurvivorPoseGraph.cpp
    "${nextdayz}/Combat/HitGeometry.cpp"
    "${nextdayz}/Data/ItemDefs.cpp"
    "${nextdayz}/Data/LootTables.cpp"
    "${nextdayz}/Inventory/Inventory.cpp"
    "${nextdayz}/Inventory/LootTransfer.cpp"
    "${nextdayz}/Net/ClientPlay.cpp"
    "${nextdayz}/Net/ClientSession.cpp"
    "${nextdayz}/Net/MasterApi.cpp"
    "${nextdayz}/Net/NetProtocol.cpp"
    "${nextdayz}/Player/FirstPersonArmPose.cpp"
    "${nextdayz}/Player/PlayerActionController.cpp"
    "${nextdayz}/Player/PlayerController.cpp"
    "${nextdayz}/Player/SurvivalSystem.cpp"
    "${nextdayz}/Sim/ItemLedger.cpp"
    "${nextdayz}/Weapons/WeaponRuntime.cpp"
    "${nextdayz}/World/TimeSystem.cpp"
)
target_compile_features(NextDayzCore PUBLIC cxx_std_20)
get_filename_component(nextdayzSource "${nextdayz}/../../.." ABSOLUTE)
target_include_directories(NextDayzCore PUBLIC "${nextdayzSource}")
if(EMSCRIPTEN)
    target_sources(NextDayzCore PRIVATE
        "${nextdayzSource}/Gameplay/Character/NextCharacterController.cpp"
        "${nextdayzSource}/Gameplay/Rig/RigLayeredAnimator.cpp"
        "${nextdayzSource}/Gameplay/Rig/RigPoseOps.cpp"
    )
    target_include_directories(NextDayzCore SYSTEM PUBLIC "${ND_CORE_HEADERS}")
    target_compile_definitions(NextDayzCore PRIVATE FMT_HEADER_ONLY)
else()
    find_package(fmt CONFIG REQUIRED)
    find_package(glm CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(NextDayzCore PUBLIC fmt::fmt glm::glm spdlog::spdlog nlohmann_json::nlohmann_json)
endif()
