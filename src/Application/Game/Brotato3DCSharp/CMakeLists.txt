file(GLOB_RECURSE brotato3DCSharpSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(Brotato3DCSharp
    ICON Brotato3D
    SOURCES ${brotato3DCSharpSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
        NextDotNet
)

gk_dotnet_managed_game(Brotato3DCSharp
    PROJECT "${GK_GAME_PROJECTS_ROOT}/Brotato3D/Scripts/Brotato3DCSharp.csproj"
    DIR brotato3d
)
