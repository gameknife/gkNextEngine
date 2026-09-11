gk_add_managed_game_application(TestFPS
    GAME_ID testfps
    PROJECT "${GK_GAME_PROJECTS_ROOT}/TestFPS/Scripts/TestFPS.csproj"
    DIR testfps
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
        NextDotNet
    LINK NextGameplay
    REGISTER_SCAD_LOADER
)
