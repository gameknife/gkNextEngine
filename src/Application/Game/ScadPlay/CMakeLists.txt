# ScadPlay: levels are .scad files and gameplay is C#, both rewritten while the game runs (by an
# agent or by hand). Everything game-specific is in projects/ScadPlay and assets/scad; see
# docs/AGENT_GUIDE/ScadPlay.md.
gk_add_managed_game_application(ScadPlay
    GAME_ID scadplay
    PROJECT "${GK_GAME_PROJECTS_ROOT}/ScadPlay/Scripts/ScadPlay.csproj"
    DIR scadplay
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
        NextDotNet
    LINK NextGameplay
    REGISTER_SCAD_LOADER
    INSTALL_RIG
)
