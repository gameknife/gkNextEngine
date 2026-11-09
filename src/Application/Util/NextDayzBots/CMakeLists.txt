# Load-test clients for a NextDayz map server: protocol only, no renderer, no map.
if(NOT "NextNet" IN_LIST GK_MODULE_NAMES)
    return()
endif()

set(nextDayzRoot "${GK_SOURCE_ROOT}/Application/Game/NextDayz")
gk_add_application(NextDayzBots NO_DEFAULT_MAIN
    SOURCES
        NextDayzBotsMain.cpp
        ${nextDayzRoot}/Net/NetProtocol.cpp
        ${nextDayzRoot}/Inventory/Inventory.cpp
        ${nextDayzRoot}/Data/ItemDefs.cpp
    MODULES NextNet
)
