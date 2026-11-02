using System.Text.Json;
using GkNext;
using GkNext.Interop;

namespace ScadPlay;

/// <summary>
/// ScadPlay: a third-person game built while it is being played.
/// </summary>
/// <remarks>
/// <para>The level is a .scad file and the gameplay is this assembly. Saving the level (or a kit it
/// uses) rebuilds it in place; republishing this project hot reloads the rules. Either way the
/// player keeps their place and the level keeps its state, which is what makes it practical to
/// sit in the game and ask an agent, in plain words, to add a bridge here or make doors open on
/// two plates -- and to watch it happen. The agent's side of that conversation is
/// <c>out/scadplay/context.json</c> plus the scadplay skill; see docs/AGENT_GUIDE/ScadPlay.md.</para>
///
/// <para>Per load the order is fixed by the engine: BeforeSceneRebuild (old world still readable:
/// capture its state; declare the rig pool) -> OnSceneLoaded (new nodes exist: index them, bind the
/// mechanisms, put the player back).</para>
/// </remarks>
[GameInstance]
public sealed class ScadPlayGame : NextGameInstance
{
    private const string DefaultLevel = "assets/scad/source/scadplay/playground.scad";

    private readonly Hud hud = new();
    private string projectRoot = string.Empty;
    private string sessionPath = string.Empty;
    private AgentContext? context;
    private bool forceContext = true;
    private float lastDelta = 1.0f / 60.0f;

    // Carried across one rebuild of the level.
    private World? previousWorld;
    private Dictionary<string, StateBag>? carriedStates;
    private (Vec3 Position, Vec3 Velocity, float Yaw)? carriedPlayer;
    private Session? reloadSession;
    private bool freshStart = true;
    private string spawnId = "start";

    private readonly Dictionary<uint, Mechanism> mechanismByNode = [];
    private LevelIndex? index;

    public LiveLevel Live { get; } = new();
    public Player Player { get; } = new();
    public FollowCamera Camera { get; } = new();
    public World? World { get; private set; }

    /// <summary>How many hot reloads of this assembly the process has seen (0 = first load).</summary>
    public int ScriptSerial { get; private set; }
    public DateTime ScriptLoadedAt { get; } = DateTime.UtcNow;

    /// <summary>Where this assembly is published, i.e. where a republish must write to reload it.</summary>
    public string PublishDirectory { get; private set; } = string.Empty;

    protected override void OnInit()
    {
        projectRoot = Paths.GetProjectRoot();
        sessionPath = Session.PathFor(projectRoot);
        context = new AgentContext(projectRoot);
        PublishDirectory = ResolvePublishDirectory();
        Live.Finished += () => forceContext = true;

        string level = ChooseLevel();
        reloadSession = Session.TryLoad(sessionPath);
        if (reloadSession != null)
        {
            // A hot reload: the previous instance of this class left its world behind.
            ScriptSerial = reloadSession.ScriptSerial + 1;
            Live.Serial = reloadSession.BuildSerial;
            level = reloadSession.Level.Length > 0 ? reloadSession.Level : level;
            carriedStates = reloadSession.States;
            carriedPlayer = (reloadSession.PlayerPosition, reloadSession.PlayerVelocity, reloadSession.PlayerYaw);
            Camera.Yaw = reloadSession.CameraYaw;
            Camera.Pitch = reloadSession.CameraPitch;
            freshStart = false;
            if (reloadSession.PointerLocked)
            {
                Camera.TogglePointerLock();
            }
            Log.Info($"[ScadPlay] hot reload #{ScriptSerial}: restoring {carriedStates.Count} mechanism states");
        }

        Live.Open(level, projectRoot, reloadSession != null ? "scripts" : "startup");
        Log.Info($"[ScadPlay] level {Live.LoadPath} ({(Live.IsLive ? "live: edits rebuild it" : "read-only copy")}); " +
                 $"agent context {context.FilePath}");
    }

    protected override void OnDestroy()
    {
        // Runs on hot reload as well as on exit; either way the next instance (if any) of this
        // process gets everything it needs to carry on.
        Session session = new()
        {
            Level = Live.LoadPath,
            PlayerPosition = Player.Position,
            PlayerVelocity = Player.Velocity,
            PlayerYaw = Player.Yaw,
            CameraYaw = Camera.Yaw,
            CameraPitch = Camera.Pitch,
            PointerLocked = Camera.PointerLocked,
            ScriptSerial = ScriptSerial,
            BuildSerial = Live.Serial,
        };
        if (World != null)
        {
            session.States = World.CaptureState();
            session.Deaths = World.Deaths;
            session.LevelTime = World.LevelTime;
            session.Checkpoint = World.CheckpointKey;
        }
        session.Save(sessionPath);
        Camera.Release();
        Player.Despawn();
    }

    protected override void BeforeSceneRebuild()
    {
        if (World != null && !freshStart)
        {
            carriedStates = World.CaptureState();
            previousWorld = World;
            if (Player.Exists)
            {
                carriedPlayer = (Player.Position, Player.Velocity, Player.Yaw);
            }
        }
        World = null;
        mechanismByNode.Clear();
        Player.Despawn();
        Player.DeclareRig();
    }

    protected override void OnSceneLoaded()
    {
        Live.OnSceneLoaded();
        index = LevelIndex.Build(Live.IsLive ? Live.LoadPath : string.Empty);

        World world = new(Player);
        if (previousWorld != null)
        {
            world.CopyProgressFrom(previousWorld);
        }
        else if (reloadSession != null)
        {
            world.RestoreProgress(reloadSession.Deaths, reloadSession.LevelTime, reloadSession.Checkpoint);
        }
        world.Bind(index, freshStart ? null : carriedStates, spawnId);
        World = world;
        foreach (Mechanism mechanism in world.Mechanisms)
        {
            mechanismByNode[mechanism.Node.Id] = mechanism;
        }

        if (carriedPlayer is { } carried && !freshStart)
        {
            Player.Spawn(carried.Position, carried.Yaw, carried.Velocity);
        }
        else
        {
            Player.Spawn(world.RespawnPosition + new Vec3(0.0f, 0.05f, 0.0f), world.RespawnYaw, Vec3.Zero);
            Camera.SnapBehind(world.RespawnYaw);
        }
        // The player's own body must not stop the camera boom or count as ground under them.
        if (NodeIds.IsValid(Player.RigRootNodeId))
        {
            Scene.SetNodeRayCastVisibleRecursive(Player.RigRootNodeId, false);
        }

        if (freshStart && world.LevelTitle.Length > 0)
        {
            world.Toast(world.LevelTitle, ToastKind.Info, 3.0f);
        }
        previousWorld = null;
        carriedStates = null;
        carriedPlayer = null;
        reloadSession = null;
        freshStart = false;
        spawnId = "start";
        forceContext = true;
    }

    protected override void OnTick(double deltaSeconds)
    {
        float delta = MathF.Min((float)deltaSeconds, 0.1f);
        lastDelta = delta;
        Live.Tick(delta);

        if (SceneReady && World != null)
        {
            TickGame(delta);
            Scene.MarkTransformDirty();
        }
        context?.Tick(delta, this, forceContext);
        forceContext = false;
    }

    private void TickGame(float delta)
    {
        World world = World!;
        if (Input.IsKeyPressed("tab"))
        {
            Camera.TogglePointerLock();
        }
        if (Input.IsKeyPressed("l"))
        {
            hud.ShowLabels = !hud.ShowLabels;
        }
        if (Input.IsKeyPressed("r"))
        {
            Restart(string.Empty, "start", "restart");
            return;
        }

        MoveAxis move = MoveAxis.Poll(arrowKeys: true);
        bool controls = world.State == PlayState.Playing;
        Camera.Update(delta, Player.Velocity, controls && move.IsMoving);
        world.InteractPressed = Input.IsKeyPressed("e");
        Player.Tick(delta, controls, move, Camera.FlatForward, Camera.FlatRight,
                    Input.IsKeyPressed("space"), Input.IsKeyDown("space"), Input.IsKeyDown("shift"));
        world.Tick(delta);

        if (world.PendingTravel is { } travel)
        {
            world.ClearTravel();
            Restart(travel.Level, travel.Spawn, $"travel:{travel.Level}");
        }
    }

    /// <summary>
    /// Starts a level from scratch: <paramref name="level"/> empty restarts this one. Nothing is
    /// carried over -- that is the difference from an edit, which keeps everything.
    /// </summary>
    private void Restart(string level, string spawn, string trigger)
    {
        freshStart = true;
        spawnId = spawn.Length > 0 ? spawn : "start";
        carriedStates = null;
        carriedPlayer = null;
        previousWorld = null;
        if (level.Length > 0)
        {
            Live.Open(level, projectRoot, trigger);
        }
        else
        {
            Live.Rebuild(trigger);
        }
    }

    protected override bool OnRenderUI()
    {
        hud.Draw(this);
        return false;
    }

    protected override bool OnOverrideCamera(ref CameraOverride camera)
    {
        if (!SceneReady || !Player.Exists)
        {
            return false;
        }
        Camera.Apply(ref camera, Player.Position, lastDelta);
        return true;
    }

    // --- for the agent context ----------------------------------------------------------------------

    /// <summary>A node as an author would name it: the piece it belongs to, or its module, and its line.</summary>
    public string DescribeNode(uint nodeId)
    {
        if (index == null || !index.ById.TryGetValue(nodeId, out LevelNode? node))
        {
            return string.Empty;
        }
        for (LevelNode? cursor = node; cursor != null; cursor = cursor.Parent)
        {
            if (mechanismByNode.TryGetValue(cursor.Id, out Mechanism? mechanism))
            {
                return Located(Mechanism.Describe(mechanism.Node), mechanism.Node.SourceLine);
            }
        }
        LevelNode root = node.Root;
        return Located(Mechanism.Describe(root), root.SourceLine);
    }

    /// <summary>What the player stands on: a moving piece by its body, anything else by the ray below.</summary>
    public string DescribeStandingOn(RayHit groundBelow)
    {
        if (Player.IsOnGround && World?.BodyOwner(Player.GroundBodyId) is Mechanism carrier)
        {
            return Located(Mechanism.Describe(carrier.Node), carrier.Node.SourceLine);
        }
        return groundBelow.IsHit ? DescribeNode(groundBelow.NodeId) : string.Empty;
    }

    private static string Located(string name, int line) => line > 0 ? $"{name} @ line {line}" : name;

    // --- startup ------------------------------------------------------------------------------------

    /// <summary>SCADPLAY_LEVEL, then Content/configs/scadplay.json, then the bundled playground.</summary>
    private static string ChooseLevel()
    {
        string? fromEnvironment = Environment.GetEnvironmentVariable("SCADPLAY_LEVEL");
        if (!string.IsNullOrWhiteSpace(fromEnvironment))
        {
            return fromEnvironment.Trim();
        }
        try
        {
            byte[] config = GameContent.ReadFile("configs/scadplay.json");
            if (config.Length > 0)
            {
                using JsonDocument document = JsonDocument.Parse(config);
                if (document.RootElement.TryGetProperty("level", out JsonElement level) &&
                    level.GetString() is { Length: > 0 } path)
                {
                    return path;
                }
            }
        }
        catch (JsonException exception)
        {
            Log.Warn($"[ScadPlay] configs/scadplay.json is not valid JSON: {exception.Message}");
        }
        return DefaultLevel;
    }

    /// <summary>
    /// The directory the host loaded this assembly from: bin/csharp/scadplay next to the executable
    /// (the per-game exe, the launcher and the editor all live in the same bin).
    /// </summary>
    private static string ResolvePublishDirectory()
    {
        string? executable = Environment.ProcessPath;
        if (string.IsNullOrEmpty(executable))
        {
            return string.Empty;
        }
        string candidate = Path.Combine(Path.GetDirectoryName(executable) ?? string.Empty, "csharp", "scadplay");
        return File.Exists(Path.Combine(candidate, "ScadPlay.dll")) ? Path.GetFullPath(candidate) : string.Empty;
    }
}
