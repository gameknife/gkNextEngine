using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// {{DisplayName}} — a first-person shooter: walk, look, fire at infected that walk you down.
/// </summary>
/// <remarks>
/// The camera is the player. There is no visible body; the infected are ScadRig characters, and
/// the only node the game moves itself is the tracer. Animation of the enemies is the engine's
/// job — this class only acquires them, points them at the player, and releases the dead.
///
/// Pointer lock is on from the first frame so look works at the screen edge and inside the editor
/// viewport. F8 ejects Play and the host frees the cursor; clicking recaptures it.
///
/// Controls: WASD moves relative to the look direction, Shift sprints, mouse looks, right mouse
/// zooms, left mouse fires, R reloads, Space restarts after a death, Escape leaves.
/// </remarks>
[GameInstance]
public sealed class {{ProjectName}}Game : NextGameInstance
{
    // --- assets. The infected rig ships with the engine; see docs/AGENT_GUIDE/ScadRig.md ---------
    private const string EnemyRig = "assets/scad/characters/nextdayz_infected.scad";

    // --- tuning. Move these into Content/configs/*.json (GameContent.ReadFile) once you iterate ---
    private const float ArenaHalfSize = 26.0f;
    private const float PlayerRadius = 0.45f;
    private const float EyeHeight = 1.7f;
    private const float WalkSpeed = 5.4f;
    private const float SprintMultiplier = 1.7f;
    private const float LookSensitivity = 0.0030f;
    private const float HipFieldOfView = 65.0f;
    private const float AimFieldOfView = 50.0f;
    private const int EnemyCapacity = 20;
    private const float EnemySpeed = 2.6f;
    private const float EnemyHealth = 60.0f;
    private const float ContactDamagePerSecond = 14.0f;
    private const float MaxHealth = 100.0f;
    private const float ShotDamage = 34.0f;
    private const float ShotRange = 45.0f;

    /// cos(18°): how far off the aim ray a target may sit and still be hit.
    private const float AimToleranceCos = 0.951f;
    private const float SpawnIntervalStart = 2.4f;
    private const float SpawnIntervalFloor = 0.55f;
    private const float SpawnRampSeconds = 100.0f;
    private const float SpawnRingRadius = 22.0f;
    private const uint RngSeed = 20260828u;

    // --- SDL button numbering, which is what the input bindings carry ---------------------------
    private const int LeftMouseButton = 1;
    private const int RightMouseButton = 3;

    private readonly Rng rng = new(RngSeed);
    private readonly ManagedImGui gui = new();
    private readonly LookController look = new(LookSensitivity);
    private readonly EnemySquad enemies = new(EnemyCapacity);
    private readonly Rifle rifle = new(magazineSize: 12, fireInterval: 0.16f, reloadSeconds: 1.7f);

    private uint enemyPoolId;

    private float positionX;
    private float positionZ;
    private float health = MaxHealth;
    private float survivedSeconds;
    private float spawnTimer;
    private int kills;
    private int best;
    private bool alive = true;
    private bool mouseJustCaptured;

    protected override void OnInit()
    {
        if (!Rig.IsAvailable())
        {
            // The manifest asks for ScadLoader and NextGameplay; a host without them refuses the
            // game in its menu, so reaching this means something else went wrong.
            Log.Error("[{{ProjectName}}] no rig subsystem — the infected will not appear");
        }

        CaptureMouse();
    }

    protected override void OnDestroy()
    {
        Input.SetRelativeMouseMode(false);
    }

    protected override void BeforeSceneRebuild()
    {
        enemyPoolId = Rig.DeclarePool(EnemyRig, EnemyCapacity);

        // A generator of its own for the layout, so the arena is identical every time the scene
        // is built no matter how many spawns the previous run drew.
        Rng layout = new(RngSeed);

        uint boxModel = SceneBuild.AddBoxModel(new(-0.5f, -0.5f, -0.5f), new(0.5f, 0.5f, 0.5f));
        uint wallMaterial = SceneBuild.AddLambertianMaterial(new(0.20f, 0.21f, 0.22f));
        uint coverMaterial = SceneBuild.AddLambertianMaterial(new(0.42f, 0.38f, 0.32f));

        SceneBuild.AddRenderNode("{{ProjectName}}_Ground",
            new RenderNodeSpec(boxModel, SceneBuild.AddLambertianMaterial(new(0.29f, 0.30f, 0.28f)))
                .WithTranslation(new Vector3(0.0f, -0.5f, 0.0f))
                .WithScale(new Vector3(ArenaHalfSize * 2.0f, 1.0f, ArenaHalfSize * 2.0f)));

        AddBox(boxModel, wallMaterial, 0.0f, 1.5f, ArenaHalfSize, ArenaHalfSize * 2.0f, 3.0f, 1.0f);
        AddBox(boxModel, wallMaterial, 0.0f, 1.5f, -ArenaHalfSize, ArenaHalfSize * 2.0f, 3.0f, 1.0f);
        AddBox(boxModel, wallMaterial, ArenaHalfSize, 1.5f, 0.0f, 1.0f, 3.0f, ArenaHalfSize * 2.0f);
        AddBox(boxModel, wallMaterial, -ArenaHalfSize, 1.5f, 0.0f, 1.0f, 3.0f, ArenaHalfSize * 2.0f);

        for (int i = 0; i < 14; i++)
        {
            float x = layout.NextFloat(-ArenaHalfSize + 5.0f, ArenaHalfSize - 5.0f);
            float z = layout.NextFloat(-ArenaHalfSize + 5.0f, ArenaHalfSize - 5.0f);
            float height = layout.NextFloat(1.0f, 2.6f);
            if (MathF.Abs(x) < 4.0f && MathF.Abs(z) < 4.0f)
            {
                continue;   // keep the spawn point clear
            }
            AddBox(boxModel, coverMaterial, x, height * 0.5f, z,
                   layout.NextFloat(1.6f, 3.4f), height, layout.NextFloat(1.6f, 3.4f));
        }

        rifle.SetTracerNode(SceneBuild.AddRenderNode("{{ProjectName}}_Tracer",
            new RenderNodeSpec(boxModel, SceneBuild.AddDiffuseLightMaterial(new(1.0f, 0.86f, 0.42f), 4.0f))
                .WithTranslation(new Vector3(0.0f, -50.0f, 0.0f))
                .WithVisible(false)));
    }

    private static void AddBox(uint model, uint material, float x, float y, float z,
                               float sizeX, float sizeY, float sizeZ)
    {
        SceneBuild.AddRenderNode("{{ProjectName}}_Block",
            new RenderNodeSpec(model, material)
                .WithTranslation(new Vector3(x, y, z))
                .WithScale(new Vector3(sizeX, sizeY, sizeZ)));
    }

    protected override void OnSceneLoaded()
    {
        Sky.Apply(skyIntensity: 120.0f, sunIntensity: 160.0f, sunRotation: 1.4f, sunElevation: 0.8f);
        CaptureMouse();
        ResetRun();
    }

    protected override void OnTick(double deltaSeconds)
    {
        if (!SceneReady)
        {
            return;
        }

        RecaptureMouseIfClicked();
        look.Update();

        float delta = MathF.Min((float)deltaSeconds, 0.1f);
        rifle.Tick(delta);

        if (!alive)
        {
            return;
        }

        survivedSeconds += delta;
        UpdatePlayerMovement(delta);
        UpdateWeapon();
        UpdateSpawns(delta);
        UpdateEnemies(delta);
    }

    private void UpdatePlayerMovement(float deltaSeconds)
    {
        MoveAxis move = MoveAxis.Poll();
        if (!move.IsMoving)
        {
            return;
        }

        float speed = WalkSpeed * (Input.IsKeyDown("shift") ? SprintMultiplier : 1.0f);
        float step = speed * deltaSeconds;
        float limit = ArenaHalfSize - 1.0f - PlayerRadius;
        Vector3 forward = look.FlatForward;
        Vector3 right = look.Right;
        positionX = Mathx.Clamp(positionX + (forward.X * move.Forward + right.X * move.Right) * step,
                                -limit, limit);
        positionZ = Mathx.Clamp(positionZ + (forward.Z * move.Forward + right.Z * move.Right) * step,
                                -limit, limit);
    }

    private void UpdateWeapon()
    {
        if (mouseJustCaptured)
        {
            return;
        }

        if (Input.IsKeyPressed("r"))
        {
            rifle.BeginReload();
        }

        if (Input.IsMouseButtonDown(LeftMouseButton) && rifle.TryFire())
        {
            FireShot();
        }
    }

    /// <summary>
    /// One hitscan shot: whatever the player is pointing at takes the damage, and the tracer draws
    /// the line either way.
    /// </summary>
    /// <remarks>
    /// A miss still draws a tracer, out to the weapon's range. Without it a missed shot produces no
    /// feedback at all and reads as the game having dropped the input.
    /// </remarks>
    private void FireShot()
    {
        Vector3 direction = look.Forward;
        int target = enemies.PickTarget(positionX, positionZ, direction, ShotRange, AimToleranceCos);
        Vector3 muzzle = new(positionX, EyeHeight, positionZ);
        Vector3 hit = target >= 0
            ? enemies.PositionOf(target)
            : new Vector3(positionX + direction.X * ShotRange,
                          EyeHeight + direction.Y * ShotRange,
                          positionZ + direction.Z * ShotRange);

        rifle.ShowTracer(muzzle, hit);
        if (target >= 0 && enemies.Damage(target, ShotDamage))
        {
            kills++;
        }
    }

    private void UpdateSpawns(float deltaSeconds)
    {
        float interval = Mathx.Lerp(SpawnIntervalStart, SpawnIntervalFloor,
                                    Mathx.Saturate(survivedSeconds / SpawnRampSeconds));

        spawnTimer += deltaSeconds;
        while (spawnTimer >= interval)
        {
            spawnTimer -= interval;
            Vector3 direction = rng.NextDirectionXZ();
            enemies.TrySpawn(enemyPoolId,
                             direction.X * SpawnRingRadius,
                             direction.Z * SpawnRingRadius,
                             EnemyHealth,
                             new Vector3(0.45f + rng.NextFloat(-0.1f, 0.1f), 0.34f, 0.30f));
        }
    }

    private void UpdateEnemies(float deltaSeconds)
    {
        int contacts = enemies.Advance(deltaSeconds, EnemySpeed, positionX, positionZ,
                                       PlayerRadius + EnemySquad.Radius);
        if (contacts == 0)
        {
            return;
        }

        health -= contacts * ContactDamagePerSecond * deltaSeconds;
        if (health > 0.0f)
        {
            return;
        }

        health = 0.0f;
        alive = false;
        best = Math.Max(best, kills);
    }

    protected override bool OnRenderUI()
    {
        gui.BeginFrame();

        DrawCrosshair();

        float healthFraction = health / MaxHealth;
        gui.Panel(new UiRect(20.0f, 20.0f, 268.0f, 96.0f), 10.0f);
        gui.ProgressBar(new UiRect(36.0f, 36.0f, 236.0f, 14.0f), healthFraction,
                        HudPalette.Bar(healthFraction));
        if (rifle.IsReloading)
        {
            gui.ProgressBar(new UiRect(36.0f, 58.0f, 236.0f, 16.0f), rifle.ReloadProgress,
                            HudPalette.Highlight, "RELOADING");
        }
        else
        {
            gui.DrawText($"AMMO {rifle.Ammo} / {rifle.MagazineSize}", 36.0f, 60.0f,
                         rifle.IsEmpty ? HudPalette.Danger : HudPalette.Text);
        }
        gui.DrawText($"kills {kills}    infected {enemies.AliveCount}    {survivedSeconds:F0}s",
                     36.0f, 84.0f, HudPalette.Muted);

        gui.DrawTextCenteredX("WASD move   SHIFT sprint   LMB fire   RMB zoom   R reload",
                              gui.ScreenSize.Y - 38.0f, HudPalette.Muted, 1.0f, shadow: true);

        if (!alive)
        {
            float y = gui.ScreenSize.Y * 0.5f - 70.0f;
            gui.PanelCenteredX(420.0f, y, 140.0f, 16.0f);
            gui.DrawTextCenteredX("YOU DIED", y + 24.0f, HudPalette.Danger, 1.8f);
            gui.DrawTextCenteredX($"kills {kills}    best {best}", y + 70.0f, HudPalette.Text, 1.15f);
            gui.DrawTextCenteredX("SPACE TO TRY AGAIN", y + 104.0f, HudPalette.Accent);
        }

        gui.EndFrame();
        return false;
    }

    /// <summary>
    /// A cross at the centre of the screen, drawn dark underneath and light on top.
    /// </summary>
    /// <remarks>
    /// The two passes are not decoration. A white cross alone disappears against a pale wall, and
    /// most of this scene is pale walls; the dark pass underneath is what makes it visible on any
    /// background at all. The gap closes when zooming, which is the only feedback that the shot
    /// has tightened.
    /// </remarks>
    private void DrawCrosshair()
    {
        float centerX = MathF.Floor(gui.ScreenSize.X * 0.5f);
        float centerY = MathF.Floor(gui.ScreenSize.Y * 0.5f);
        bool aiming = Input.IsMouseButtonDown(RightMouseButton);
        float gap = aiming ? 5.0f : 9.0f;
        Color mark = aiming ? HudPalette.Highlight : Color.White;

        DrawCrosshairPass(centerX, centerY, gap - 1.0f, 4.0f, HudPalette.Shadow);
        DrawCrosshairPass(centerX, centerY, gap, 2.0f, mark);
    }

    private void DrawCrosshairPass(float centerX, float centerY, float gap, float thickness, Color color)
    {
        const float arm = 8.0f;
        float half = thickness * 0.5f;
        gui.DrawList.AddRectFilled(new UiRect(centerX - gap - arm, centerY - half, arm, thickness), color);
        gui.DrawList.AddRectFilled(new UiRect(centerX + gap, centerY - half, arm, thickness), color);
        gui.DrawList.AddRectFilled(new UiRect(centerX - half, centerY - gap - arm, thickness, arm), color);
        gui.DrawList.AddRectFilled(new UiRect(centerX - half, centerY + gap, thickness, arm), color);
    }

    protected override bool OnOverrideCamera(ref CameraOverride camera)
    {
        Vector3 forward = look.Forward;
        camera.Position = new Vector3(positionX, EyeHeight, positionZ);
        camera.Target = new Vector3(positionX + forward.X, EyeHeight + forward.Y, positionZ + forward.Z);
        camera.Up = Vector3.Up;
        camera.FieldOfView = Input.IsMouseButtonDown(RightMouseButton) ? AimFieldOfView : HipFieldOfView;
        return true;
    }

    protected override bool OnInputEvent(in InputEvent inputEvent)
    {
        if (inputEvent.Type != InputEventType.KeyDown || inputEvent.IsRepeat)
        {
            return false;
        }

        if (inputEvent.KeyCode == KeyCodes.Escape)
        {
            // In the launcher and the editor this returns to the host; a standalone build closes.
            Engine.RequestClose();
            return true;
        }

        if (!alive && inputEvent.KeyCode == KeyCodes.Space)
        {
            ResetRun();
            return true;
        }

        return false;
    }

    private void ResetRun()
    {
        enemies.Reset();
        rifle.Reset();
        look.Reset(0.0f, -0.12f);
        rng.Reset(RngSeed);

        positionX = 0.0f;
        positionZ = 0.0f;
        health = MaxHealth;
        survivedSeconds = 0.0f;
        spawnTimer = 0.0f;
        kills = 0;
        alive = true;
    }

    private void CaptureMouse()
    {
        Input.SetRelativeMouseMode(true);
        look.SuppressNextDelta();
    }

    /// <summary>
    /// Pointer lock can drop — Alt-Tab, the editor ejecting then resuming — and a click is the
    /// obvious way to take it back. Ignored while already locked, so firing does not re-issue it.
    /// </summary>
    private void RecaptureMouseIfClicked()
    {
        mouseJustCaptured = false;
        if (Input.IsRelativeMouseMode())
        {
            return;
        }
        if (Input.IsMouseButtonPressed(LeftMouseButton) || Input.IsMouseButtonPressed(RightMouseButton))
        {
            CaptureMouse();
            mouseJustCaptured = true;
        }
    }
}
