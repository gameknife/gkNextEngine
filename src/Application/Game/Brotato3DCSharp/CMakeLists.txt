gk_add_managed_game_application(Brotato3DCSharp
    ICON Brotato3D
    GAME_ID brotato3d
    PROJECT "${GK_GAME_PROJECTS_ROOT}/Brotato3D/Scripts/Brotato3DCSharp.csproj"
    DIR brotato3d
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
        NextDotNet
    REGISTER_SCAD_LOADER
)
