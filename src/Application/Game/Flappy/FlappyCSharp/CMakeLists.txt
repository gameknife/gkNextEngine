gk_add_managed_game_application(FlappyCSharp
    GAME_ID flappy
    PROJECT "${GK_GAME_PROJECTS_ROOT}/Flappy/Scripts/FlappyCSharp.csproj"
    DIR flappy
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        NextDotNet
)
