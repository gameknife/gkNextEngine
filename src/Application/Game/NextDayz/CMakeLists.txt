file(GLOB_RECURSE nextDayzSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")
# The game host owns the async navigation worker; compiling this large TU
# separately keeps MSVC unity objects below their section-count limit.
set_source_files_properties(NextDayzGameInstance.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

gk_add_application(NextDayz
    SOURCES ${nextDayzSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
    LINK NextGameplay
)
