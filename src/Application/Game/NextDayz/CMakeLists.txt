file(GLOB_RECURSE nextDayzSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")
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
    LINK NextGameplay
)
