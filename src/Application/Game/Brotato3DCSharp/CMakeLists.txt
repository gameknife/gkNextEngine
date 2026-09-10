gk_add_managed_game_application(Brotato3DCSharp
    ICON Brotato3D
    MANIFEST "assets/projects/Brotato3D/brotato3d.game.json"
    PROJECT "${GK_GAME_PROJECTS_ROOT}/Brotato3D/Scripts/Brotato3DCSharp.csproj"
    DIR brotato3d
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
        NextDotNet
    REGISTER_SCAD_LOADER
)
