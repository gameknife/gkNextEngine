# {{DisplayName}}

Generated from the gkNextEngine `{{TemplateId}}` game template.

Everything this game owns lives in this directory, `projects/{{ProjectName}}/`:

| | |
|---|---|
| `{{GameId}}.game.json` | The manifest: window, assembly, required modules, initial scene, hot reload. |
| `Content/` | The game's own configs, sounds, textures and scenes. Reach them with `GameContent.Path("...")` / `GameContent.ReadFile("...")`. |
| `Scripts/` | The C# project, `{{ProjectName}}.csproj`. |
| Assembly | `{{ProjectName}}.dll`, published into `<bin>/csharp/{{GameId}}/` |

At runtime the manifest and `Content/` are copied to `assets/projects/{{ProjectName}}/`, beside every
other asset, so they are paked and packaged like the rest. `Scripts/` never is: the published
assembly is what runs. Engine assets (`assets/scad/...`, `assets/models/...`) are still named by
their own path.

## Run it

- `gnb run gkNextLauncher`, then pick **{{DisplayName}}**.
- Or in `gkNextEditor`: choose it in the play toolbar, `F5` plays, `F8` ejects back to the editor
  so you can inspect the running game's scene.

## Change it

1. `gnb dotnet sln` once, so this project joins `assets/csharp/GkNextManaged.sln` — open the
   *solution*, never the bare csproj, or the IDE will not load `GkNext.Engine` alongside it.
2. Edit the C# or anything in `Content/`, then press **Rebuild C#** in the launcher or the editor:
   it republishes the assembly and refreshes the runtime copy of `Content/`. With `hotReload` on (it
   is, in the manifest) a running game picks the new assembly up without restarting.

## Give it its own executable (optional)

It does not need one: the launcher and the editor run it straight from the manifest. When you want
a standalone `.exe`, add `src/Application/Game/{{ProjectName}}/` with a CMakeLists and a ~15-line
`Main.cpp` — see `docs/AGENT_GUIDE/CSharpGameDevelopment.md` section 2.

## Before you write a helper

`GkNext.Engine` already carries the parts every game needs, and each one is there because writing it
per game meant writing the same bug per game:

| | |
|---|---|
| `Rng` | Deterministic random. `System.Random` changes its sequence between runtimes, so the same seed stops meaning the same run. |
| `MoveAxis.Poll()` | WASD, arrows and the gamepad stick as one normalised stick. |
| `Mathx` / `Quat` | `Clamp`, `Lerp`, `TurnTowards` (shortest way round an angle), `Quat.AroundY`, `Quat.LookAlong`. |
| `Sky.Apply(...)` | Sky and sun for a procedurally built scene, which starts with neither. |
| `ManagedImGui` + `HudPalette` | Panels, buttons, progress bars, shadowed text — one draw list per frame. |
| `GameContent` | This project's `Content/`, as paths every engine API accepts. |
| `SceneReady` | On `NextGameInstance`. False until the scene is committed and node ids resolve. |

## Where to look next

- `docs/AGENT_GUIDE/CSharpGameDevelopment.md` — the whole managed game surface, written for people
  arriving from Unity.
- `docs/AGENT_GUIDE/DotNetBindings.md` — when the engine call you want does not exist yet.
- `projects/Flappy/` — a complete worked example, content and all.
