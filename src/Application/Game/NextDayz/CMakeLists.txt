add_subdirectory(Core)

file(GLOB_RECURSE nextDayzSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")
list(FILTER nextDayzSources EXCLUDE REGEX
    "/(Combat/HitGeometry|Core/CharacterPoseGraph|Data/(ItemDefs|LootTables)|Inventory/(Inventory|LootTransfer)|Net/(ClientSession|MasterApi|NetProtocol)|Player/(SurvivalSystem|PlayerController|PlayerActionController|FirstPersonArmPose)|Sim/ItemLedger|Weapons/WeaponRuntime|World/TimeSystem)\\.cpp$")
# The game host owns the async navigation worker; compiling this large TU
# separately keeps MSVC unity objects below their section-count limit.
set_source_files_properties(NextDayzGameInstance.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

# Multiplayer (dedicated server + clients) where the NextNet module exists;
# elsewhere the game builds single-player only (GK_MODULE_NEXTNET undefined).
set(nextDayzModules ${GK_STANDARD_RUNTIME_MODULES} ScadLoader)
if("NextNet" IN_LIST GK_MODULE_NAMES)
    list(APPEND nextDayzModules NextNet)
endif()

gk_add_application(NextDayz
    SOURCES ${nextDayzSources}
    MODULES ${nextDayzModules}
    LINK NextDayzCore NextGameplay
)

if(NOT ANDROID AND NOT IOS)
    add_executable(NextDayzProtocolGolden EXCLUDE_FROM_ALL
        "${GK_SOURCE_ROOT}/Application/Web/NextDayzWebCore/ProtocolProbe.cpp")
    target_link_libraries(NextDayzProtocolGolden PRIVATE NextDayzCore)
    add_executable(NextDayzCoreRules EXCLUDE_FROM_ALL
        "${GK_SOURCE_ROOT}/Application/Web/NextDayzWebCore/CoreRulesProbe.cpp")
    target_link_libraries(NextDayzCoreRules PRIVATE NextDayzCore)
    add_executable(NextDayzRigPoseProbe EXCLUDE_FROM_ALL
        "${GK_SOURCE_ROOT}/Application/Web/NextDayzWebCore/RigPoseProbe.cpp")
    target_sources(NextDayzRigPoseProbe PRIVATE
        "${GK_SOURCE_ROOT}/Gameplay/Rig/RigLayeredAnimator.cpp"
        "${GK_SOURCE_ROOT}/Gameplay/Rig/RigPoseOps.cpp")
    target_link_libraries(NextDayzRigPoseProbe PRIVATE NextDayzCore)
endif()
