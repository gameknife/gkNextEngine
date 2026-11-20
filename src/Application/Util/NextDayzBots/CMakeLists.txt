# Load-test clients for a NextDayz map server: protocol only, no renderer, no map.
if(NOT "NextNet" IN_LIST GK_MODULE_NAMES)
    return()
endif()

gk_add_application(NextDayzBots NO_DEFAULT_MAIN
    SOURCES NextDayzBotsMain.cpp
    MODULES NextNet
    LINK NextDayzCore
)
