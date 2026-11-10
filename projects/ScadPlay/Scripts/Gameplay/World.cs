using GkNext;
using GkNext.Interop;

namespace ScadPlay;

public enum PlayState
{
    Playing,
    Dead,
    Won,
}

public enum ToastKind
{
    Info,
    Good,
    Bad,
}

/// <summary>
/// The running level: its mechanisms, the signals between them, and the rules every game in this
/// kit shares -- spawning, checkpoints, dying, finishing, coins.
/// </summary>
/// <remarks>
/// A World is rebuilt from scratch on every load of the level, but what the player has done
/// survives: <see cref="CaptureState"/> before the rebuild, carried states into
/// <see cref="Bind"/> after it. That is what lets an agent rewrite the level under a player who
/// has already opened three doors.
/// </remarks>
public sealed class World
{
    private const int MaxEvents = 40;
    private const float DeathSeconds = 1.2f;
    private const float BottomPlaneMargin = 0.3f;

    private readonly Dictionary<string, Mechanism> byId = new(StringComparer.Ordinal);
    private readonly HashSet<string> warned = new(StringComparer.Ordinal);
    private readonly List<(float Time, string Text)> events = [];
    private float deathTimer;
    private float winTimer;
    private string pendingNext = string.Empty;
    private float levelBottom = -50.0f;

    public World(Player player)
    {
        Player = player;
    }

    public Player Player { get; }

    public readonly List<Mechanism> Mechanisms = [];

    /// <summary>Module calls that look like gameplay (they take an id) but have no C# class.</summary>
    public readonly List<string> Unbound = [];

    public readonly List<string> Warnings = [];

    public readonly List<(string Text, ToastKind Kind, float Remaining)> Toasts = [];

    public string LevelTitle { get; set; } = string.Empty;
    public string LevelGoal { get; set; } = string.Empty;

    public PlayState State { get; private set; } = PlayState.Playing;
    public string DeathReason { get; private set; } = string.Empty;
    public int Coins { get; set; }
    public int CoinsTotal { get; set; }
    public int Deaths { get; private set; }
    public float LevelTime { get; private set; }

    /// <summary>Where a respawn puts the player (engine space) and which way they face.</summary>
    public Vec3 RespawnPosition { get; private set; }
    public float RespawnYaw { get; private set; }
    public string CheckpointKey { get; private set; } = string.Empty;

    /// <summary>The interaction on offer ("E 扳动拉杆"): the nearest piece that offered one last frame.</summary>
    public string Prompt { get; private set; } = string.Empty;
    private Mechanism? offerWinner;
    private Mechanism? offerCandidate;
    private string offerCandidatePrompt = string.Empty;
    private float offerCandidateDistance = float.MaxValue;

    /// <summary>Text of a sign the player is standing near.</summary>
    public string SignText { get; set; } = string.Empty;

    public bool InteractPressed { get; set; }

    /// <summary>Set by portals and goals; the game performs the travel after the tick.</summary>
    public (string Level, string Spawn)? PendingTravel { get; private set; }

    public IReadOnlyList<(float Time, string Text)> Events => events;

    // --- building ------------------------------------------------------------------------------

    /// <summary>
    /// Creates the C# piece for every node whose module has a class, restores what the previous
    /// build of the level had reached, then lets every piece resolve its wiring.
    /// </summary>
    public void Bind(LevelIndex index, IReadOnlyDictionary<string, StateBag>? carried, string spawnId)
    {
        float minY = float.MaxValue;
        foreach (LevelNode root in index.Roots())
        {
            Bounds bounds = Scene.GetNodeBounds(root.Id, false);
            if (bounds.IsValid)
            {
                minY = MathF.Min(minY, bounds.Min.Y);
            }
        }
        levelBottom = minY == float.MaxValue ? -50.0f : minY;

        foreach (LevelNode node in index.Nodes)
        {
            Mechanism? mechanism = MechanismTypes.Create(node.Name);
            if (mechanism == null)
            {
                if (node.Params.Has("id") && !node.Name.StartsWith("sp_part_", StringComparison.Ordinal) &&
                    !Unbound.Contains(node.Name))
                {
                    Unbound.Add(node.Name);
                }
                continue;
            }
            mechanism.Bind(this, node);
            Mechanisms.Add(mechanism);
            if (mechanism.Id.Length > 0 && !byId.TryAdd(mechanism.Id, mechanism))
            {
                Warn($"id \"{mechanism.Id}\" is used by more than one piece; listeners hear the first");
            }
        }

        foreach (Mechanism mechanism in Mechanisms)
        {
            foreach (string id in mechanism.On.Concat(mechanism.Any))
            {
                if (!IdExists(id))
                {
                    Warn($"{Mechanism.Describe(mechanism.Node)} listens to \"{id}\", which no piece has");
                }
            }
        }

        foreach (Mechanism mechanism in Mechanisms)
        {
            Guard(mechanism, "OnStart", () => mechanism.OnStart());
        }
        if (carried != null)
        {
            foreach (Mechanism mechanism in Mechanisms)
            {
                if (carried.TryGetValue(mechanism.Key, out StateBag? state))
                {
                    Guard(mechanism, "Load", () => mechanism.Load(state));
                }
            }
        }

        PlaceRespawn(spawnId);
    }

    private void PlaceRespawn(string spawnId)
    {
        Mechanism? spawn = null;
        foreach (Mechanism mechanism in Mechanisms)
        {
            if (mechanism is Spawn)
            {
                if (mechanism.Id == spawnId)
                {
                    spawn = mechanism;
                    break;
                }
                spawn ??= mechanism;
            }
        }

        if (CheckpointKey.Length > 0 && Mechanisms.Find(m => m.Key == CheckpointKey) is Checkpoint checkpoint)
        {
            SetRespawn(checkpoint.Position, checkpoint.FacingYaw, checkpoint.Key);
        }
        else if (spawn is Spawn start)
        {
            SetRespawn(start.Position, start.FacingYaw, string.Empty);
        }
        else
        {
            Warn("the level has no sp_spawn; starting at the origin");
            SetRespawn(new Vec3(0.0f, 1.0f, 0.0f), 0.0f, string.Empty);
        }
    }

    public void SetRespawn(Vec3 position, float yaw, string checkpointKey)
    {
        RespawnPosition = position;
        RespawnYaw = yaw;
        CheckpointKey = checkpointKey;
    }

    /// <summary>Snapshot of every piece's state, keyed so the next build of the level finds it.</summary>
    public Dictionary<string, StateBag> CaptureState()
    {
        Dictionary<string, StateBag> states = new(StringComparer.Ordinal);
        foreach (Mechanism mechanism in Mechanisms)
        {
            StateBag bag = new();
            Guard(mechanism, "Save", () => mechanism.Save(bag));
            if (!bag.IsEmpty)
            {
                states[mechanism.Key] = bag;
            }
        }
        return states;
    }

    /// <summary>Progress that is the game's, not a piece's: carried across rebuilds with the pieces.</summary>
    public void CopyProgressFrom(World previous)
    {
        Deaths = previous.Deaths;
        LevelTime = previous.LevelTime;
        CheckpointKey = previous.CheckpointKey;
        State = previous.State == PlayState.Won ? PlayState.Won : PlayState.Playing;
        events.AddRange(previous.events);
    }

    public void RestoreProgress(int deaths, float levelTime, string checkpointKey)
    {
        Deaths = deaths;
        LevelTime = levelTime;
        CheckpointKey = checkpointKey;
    }

    // --- signals -------------------------------------------------------------------------------

    public bool IdExists(string id)
    {
        if (id.EndsWith('*'))
        {
            string prefix = id[..^1];
            return Mechanisms.Exists(m => m.Id.StartsWith(prefix, StringComparison.Ordinal));
        }
        return byId.ContainsKey(id);
    }

    /// <summary>
    /// Is the piece called <paramref name="id"/> active? "prefix*" asks whether every piece whose id
    /// starts with prefix is (and there is at least one).
    /// </summary>
    public bool Signal(string id)
    {
        if (id.EndsWith('*'))
        {
            string prefix = id[..^1];
            bool any = false;
            foreach (Mechanism mechanism in Mechanisms)
            {
                if (mechanism.Id.StartsWith(prefix, StringComparison.Ordinal))
                {
                    if (!mechanism.Active)
                    {
                        return false;
                    }
                    any = true;
                }
            }
            return any;
        }
        return byId.TryGetValue(id, out Mechanism? found) && found.Active;
    }

    public Mechanism? Find(string id) => byId.GetValueOrDefault(id);

    private readonly Dictionary<uint, Mechanism> bodyOwners = [];

    /// <summary>Records which piece a kinematic body moves with, so "standing on" can name it.</summary>
    public void RegisterBody(uint bodyId, Mechanism owner) => bodyOwners[bodyId] = owner;

    public Mechanism? BodyOwner(uint bodyId) => bodyOwners.GetValueOrDefault(bodyId);

    // --- per frame ------------------------------------------------------------------------------

    public void Tick(float deltaSeconds)
    {
        SignText = string.Empty;
        offerCandidate = null;
        offerCandidatePrompt = string.Empty;
        offerCandidateDistance = float.MaxValue;

        if (State == PlayState.Playing)
        {
            LevelTime += deltaSeconds;
        }

        // Not through Guard: that takes a closure, and this runs for every piece every frame.
        foreach (Mechanism mechanism in Mechanisms)
        {
            if (mechanism.Fault != null)
            {
                continue;
            }
            try
            {
                mechanism.OnTick(deltaSeconds);
            }
            catch (Exception exception)
            {
                Park(mechanism, "OnTick", exception);
            }
        }

        offerWinner = offerCandidate;
        Prompt = offerCandidatePrompt;

        if (State == PlayState.Playing && FellOut())
        {
            Kill("掉出了世界");
        }

        if (State == PlayState.Dead)
        {
            deathTimer -= deltaSeconds;
            if (deathTimer <= 0.0f)
            {
                Player.Teleport(RespawnPosition, RespawnYaw);
                State = PlayState.Playing;
            }
        }
        else if (State == PlayState.Won && pendingNext.Length > 0)
        {
            winTimer -= deltaSeconds;
            if (winTimer <= 0.0f)
            {
                Travel(pendingNext, "start");
                pendingNext = string.Empty;
            }
        }

        for (int i = Toasts.Count - 1; i >= 0; i--)
        {
            (string text, ToastKind kind, float remaining) = Toasts[i];
            remaining -= deltaSeconds;
            if (remaining <= 0.0f)
            {
                Toasts.RemoveAt(i);
            }
            else
            {
                Toasts[i] = (text, kind, remaining);
            }
        }
    }

    public void ClearTravel() => PendingTravel = null;

    /// <summary>
    /// The engine puts an invisible floor at the bottom of the level's bounds, so a player who
    /// misses every platform does not fall forever -- they land on nothing anyone can see. Standing
    /// there, with no level geometry under the feet, is what falling out of the world looks like.
    /// </summary>
    private bool FellOut()
    {
        Vec3 feet = Player.Position;
        if (feet.Y > levelBottom + BottomPlaneMargin)
        {
            return false;
        }
        RayHit below = Scene.RayCast((feet + new Vec3(0.0f, 0.25f, 0.0f)).Gk(), new Vector3(0.0f, -1.0f, 0.0f), 0.6f);
        return !below.IsHit;
    }

    /// <summary>
    /// Offers an interaction. The closest offer of a frame becomes the prompt; pressing the key
    /// triggers the piece whose prompt was on screen, so two pieces in reach never both fire.
    /// </summary>
    public bool OfferInteraction(Mechanism mechanism, string prompt, float distance)
    {
        if (distance < offerCandidateDistance)
        {
            offerCandidate = mechanism;
            offerCandidatePrompt = prompt;
            offerCandidateDistance = distance;
        }
        return InteractPressed && ReferenceEquals(offerWinner, mechanism);
    }

    // --- rules ----------------------------------------------------------------------------------

    public void Kill(string reason)
    {
        if (State != PlayState.Playing)
        {
            return;
        }
        State = PlayState.Dead;
        DeathReason = reason;
        deathTimer = DeathSeconds;
        Deaths++;
        Player.OnDied();
        Log($"玩家死亡：{reason}");
        Toast($"失败：{reason}", ToastKind.Bad, DeathSeconds);
    }

    public void CompleteLevel(string next)
    {
        if (State == PlayState.Won)
        {
            return;
        }
        State = PlayState.Won;
        pendingNext = next;
        winTimer = 2.5f;
        Player.OnWon();
        Log(next.Length > 0 ? $"通关，前往 {next}" : "通关");
        Toast($"通关！用时 {LevelTime:F1} 秒", ToastKind.Good, 4.0f);
    }

    /// <summary>True once this world has asked to be replaced; it is gone a frame or two later.</summary>
    public bool IsLeaving { get; private set; }

    /// <summary>Asks for another level (or this one, with an empty name). Once per world: the load is
    /// deferred to the end of the frame, and a portal the player is still standing in would ask again.</summary>
    public void Travel(string level, string spawn)
    {
        if (IsLeaving)
        {
            return;
        }
        IsLeaving = true;
        PendingTravel = (level, spawn);
        Log(level.Length > 0 ? $"前往 {level}（出生点 {spawn}）" : $"回到出生点 {spawn}");
    }

    public void Log(string text)
    {
        events.Add((LevelTime, text));
        if (events.Count > MaxEvents)
        {
            events.RemoveAt(0);
        }
        GkNext.Log.Info($"[ScadPlay] {text}");
    }

    public void Toast(string text, ToastKind kind = ToastKind.Info, float seconds = 2.5f)
    {
        Toasts.RemoveAll(t => t.Text == text);
        Toasts.Add((text, kind, seconds));
        if (Toasts.Count > 4)
        {
            Toasts.RemoveAt(0);
        }
    }

    /// <summary>A problem with the level an author should fix; reported once, to the log and the agent.</summary>
    public void Warn(string text)
    {
        if (warned.Add(text))
        {
            Warnings.Add(text);
            GkNext.Log.Warn($"[ScadPlay] {text}");
        }
    }

    /// <summary>
    /// Runs one piece's code and parks the piece if it throws. Gameplay is edited live, by people and
    /// by agents; one broken class must not take the rest of the level down with it, and the
    /// exception text is exactly what the author needs to see.
    /// </summary>
    private void Guard(Mechanism mechanism, string what, Action action)
    {
        try
        {
            action();
        }
        catch (Exception exception)
        {
            Park(mechanism, what, exception);
        }
    }

    private void Park(Mechanism mechanism, string what, Exception exception)
    {
        string frame = exception.StackTrace?.Split('\n').FirstOrDefault()?.Trim() ?? string.Empty;
        mechanism.Fault = $"{what}: {exception.GetType().Name}: {exception.Message} {frame}".Trim();
        Warn($"{Mechanism.Describe(mechanism.Node)} threw in {mechanism.Fault}");
    }
}
