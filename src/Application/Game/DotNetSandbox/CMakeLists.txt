gk_add_managed_game_application(DotNetSandbox
    GAME_ID sandbox
    PROJECT "${GK_DOTNET_MANAGED_ROOT}/GkNext.Game/GkNext.Game.csproj"
    DIR game
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        NextDotNet
)
