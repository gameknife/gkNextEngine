gk_add_application(NextDayzBake NO_DEFAULT_MAIN
    SOURCES BakeProbe.cpp BakeGlb.cpp BakeMap.cpp BakeRig.cpp BakeShared.cpp
            ../../Game/NextDayz/Data/ItemDefs.cpp
            ../../Game/NextDayz/World/WorldAnchorRegistry.cpp
            ../../Game/NextDayz/World/WorldAnchorData.cpp
    MODULES ScadLoader GltfLoader
)
# The unity object of the glTF writer and the map baker exceeds MSVC's default section count.
if(MSVC)
    target_compile_options(NextDayzBake PRIVATE /bigobj)
endif()
