file(GLOB_RECURSE nextRaidenSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(NextRaiden
    SOURCES ${nextRaidenSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
    LINK NextGameplay
)
