gk_add_application(NextDayzBake NO_DEFAULT_MAIN
    SOURCES BakeProbe.cpp BakeGlb.cpp BakeMap.cpp BakeRig.cpp BakeShared.cpp
            ../../Game/NextDayz/Data/ItemDefs.cpp
            ../../Game/NextDayz/World/WorldAnchorRegistry.cpp
    MODULES ScadLoader GltfLoader
)
