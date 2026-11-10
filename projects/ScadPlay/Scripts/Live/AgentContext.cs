using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.RegularExpressions;
using GkNext;
using GkNext.Interop;

namespace ScadPlay;

/// <summary>
/// Writes what an agent needs to edit this game while it runs: where the player is and looks,
/// how the last level build went, which C# is loaded, and what the gameplay is doing.
/// </summary>
/// <remarks>
/// <para>One file, <c>out/scadplay/context.json</c> under the repository root, replaced by rename so
/// a reader never sees half of it. Rewritten a few times a second, and only when something in it
/// changed. The format is the contract in docs/AGENT_GUIDE/ScadPlay.md -- change both together.</para>
///
/// <para>Every position and direction is in the level file's own space (SCAD: metres, Z up), so
/// the numbers go straight into a translate([x, y, z]). Nobody converts from the engine's Y-up.</para>
/// </remarks>
public sealed partial class AgentContext
{
    public const int Version = 1;
    private const float WriteInterval = 0.25f;
    private const float HeartbeatSeconds = 5.0f;

    private readonly string directory;
    private readonly string path;
    private byte[] lastContent = [];
    private float timer;
    private float sinceWrite;

    public AgentContext(string projectRoot)
    {
        directory = Path.Combine(projectRoot.Length > 0 ? projectRoot : Directory.GetCurrentDirectory(), "out", "scadplay");
        path = Path.Combine(directory, "context.json");
    }

    public string FilePath => path;

    /// <summary>Writes when due; <paramref name="force"/> writes now (after a build or a reload).</summary>
    public void Tick(float deltaSeconds, ScadPlayGame game, bool force)
    {
        timer += deltaSeconds;
        sinceWrite += deltaSeconds;
        if (!force && timer < WriteInterval)
        {
            return;
        }
        timer = 0.0f;

        byte[] content = Serialize(game, includeClock: false);
        if (!force && sinceWrite < HeartbeatSeconds && content.AsSpan().SequenceEqual(lastContent))
        {
            return;
        }
        lastContent = content;
        sinceWrite = 0.0f;
        Write(Serialize(game, includeClock: true));
    }

    private void Write(byte[] bytes)
    {
        try
        {
            Directory.CreateDirectory(directory);
            string temporary = path + ".tmp";
            File.WriteAllBytes(temporary, bytes);
            File.Move(temporary, path, overwrite: true);
        }
        catch (IOException exception)
        {
            Log.Warn($"[ScadPlay] could not write {path}: {exception.Message}");
        }
        catch (UnauthorizedAccessException exception)
        {
            Log.Warn($"[ScadPlay] could not write {path}: {exception.Message}");
        }
    }

    private static readonly JsonWriterOptions WriterOptions = new()
    {
        Indented = true,
        // Chinese level text and quoted module ids stay readable; this is a file people read.
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    private static byte[] Serialize(ScadPlayGame game, bool includeClock)
    {
        using MemoryStream stream = new();
        using (Utf8JsonWriter writer = new(stream, WriterOptions))
        {
            writer.WriteStartObject();
            writer.WriteNumber("version", Version);
            writer.WriteString("game", "scadplay");
            writer.WriteNumber("pid", Environment.ProcessId);
            if (includeClock)
            {
                writer.WriteString("updated", DateTime.UtcNow.ToString("o"));
            }
            writer.WriteString("protocol", "docs/AGENT_GUIDE/ScadPlay.md");
            WriteScene(writer, game);
            WritePlayer(writer, game);
            WriteView(writer, game);
            WriteBuild(writer, game.Live);
            WriteScripts(writer, game);
            WriteGameplay(writer, game);
            writer.WriteEndObject();
        }
        return CompactNumberArrays(stream.ToArray());
    }

    /// <summary>Puts [x, y, z] back on one line: the indented writer gives every number its own.</summary>
    private static byte[] CompactNumberArrays(byte[] json)
    {
        string text = Encoding.UTF8.GetString(json);
        text = NumberArray().Replace(text, match => "[" + Regex.Replace(match.Groups[1].Value, @"\s+", " ") + "]");
        return Encoding.UTF8.GetBytes(text);
    }

    [GeneratedRegex(@"\[\s*(-?[0-9.eE+-]+(?:,\s*-?[0-9.eE+-]+)*)\s*\]")]
    private static partial Regex NumberArray();

    private static void WriteScene(Utf8JsonWriter writer, ScadPlayGame game)
    {
        LiveLevel live = game.Live;
        writer.WriteStartObject("scene");
        writer.WriteString("source", live.LoadPath);
        writer.WriteString("level", live.LevelName);
        writer.WriteBoolean("live", live.IsLive);
        writer.WriteString("units", "SCAD space: metres, Z up; numbers here go straight into translate([x, y, z])");
        writer.WriteStartArray("files");
        foreach (string file in live.Watched)
        {
            writer.WriteStringValue(file);
        }
        writer.WriteEndArray();
        writer.WriteEndObject();
    }

    private static void WritePlayer(Utf8JsonWriter writer, ScadPlayGame game)
    {
        Player player = game.Player;
        writer.WriteStartObject("player");
        if (!player.Exists)
        {
            writer.WriteBoolean("spawned", false);
            writer.WriteEndObject();
            return;
        }
        Vec3 feet = ScadSpace.ToScad(player.Position);
        Vec3 forward = ScadSpace.ToScad(player.Forward);
        writer.WriteBoolean("spawned", true);
        WriteVector(writer, "pos", feet);

        // The surface under the feet, even mid-jump: that is the height to build on.
        RayHit ground = Scene.RayCast((player.Position + new Vec3(0.0f, 0.5f, 0.0f)).Gk(), new Vector3(0.0f, -1.0f, 0.0f), 200.0f);
        if (ground.IsHit)
        {
            writer.WriteNumber("ground_z", Round(ScadSpace.ToScad(ground.Position.N()).Z));
        }
        else
        {
            writer.WriteNull("ground_z");
        }
        WriteVector(writer, "forward", new Vec3(forward.X, forward.Y, 0.0f), 2, 3);
        writer.WriteNumber("yaw_deg", Round(ScadSpace.ScadYawDegrees(player.Forward), 1));
        writer.WriteBoolean("on_ground", player.IsOnGround);
        writer.WriteString("standing_on", game.DescribeStandingOn(ground));
        writer.WriteEndObject();
    }

    private static void WriteView(Utf8JsonWriter writer, ScadPlayGame game)
    {
        FollowCamera camera = game.Camera;
        Vec3 forward = ScadSpace.ToScad(camera.Forward);
        writer.WriteStartObject("view");
        WriteVector(writer, "forward", new Vec3(forward.X, forward.Y, 0.0f), 2, 3);
        writer.WriteNumber("yaw_deg", Round(ScadSpace.ScadYawDegrees(camera.FlatForward), 1));
        writer.WriteNumber("pitch_deg", Round(-camera.Pitch * 180.0f / MathF.PI, 1));
        // What the middle of the screen is on: for "put it over there".
        RayHit hit = Scene.RayCast(camera.Position.Gk(), camera.Forward.Gk(), 250.0f);
        if (hit.IsHit)
        {
            WriteVector(writer, "hit", ScadSpace.ToScad(hit.Position.N()));
            writer.WriteString("hit_node", game.DescribeNode(hit.NodeId));
        }
        else
        {
            writer.WriteNull("hit");
            writer.WriteString("hit_node", string.Empty);
        }
        writer.WriteEndObject();
    }

    private static void WriteBuild(Utf8JsonWriter writer, LiveLevel live)
    {
        writer.WriteStartObject("build");
        writer.WriteNumber("serial", live.Serial);
        writer.WriteBoolean("building", live.Building);
        writer.WriteBoolean("ok", live.Ok);
        writer.WriteString("error", live.Error);
        writer.WriteStartArray("warnings");
        foreach (string warning in live.Warnings)
        {
            writer.WriteStringValue(warning);
        }
        writer.WriteEndArray();
        writer.WriteNumber("warning_count", live.WarningCount);
        writer.WriteNumber("nodes", live.Nodes);
        writer.WriteNumber("triangles", live.Triangles);
        writer.WriteNumber("ms", Math.Round(live.Milliseconds, 1));
        writer.WriteNumber("eval_ms", Math.Round(live.EvaluateMilliseconds, 1));
        writer.WriteString("trigger", live.Trigger);
        writer.WriteBoolean("scene_shown", live.HasScene);
        writer.WriteEndObject();
    }

    private static void WriteScripts(Utf8JsonWriter writer, ScadPlayGame game)
    {
        writer.WriteStartObject("scripts");
        writer.WriteNumber("serial", game.ScriptSerial);
        writer.WriteString("loaded_at", game.ScriptLoadedAt.ToString("o"));
        writer.WriteString("project", "projects/ScadPlay/Scripts/ScadPlay.csproj");
        writer.WriteString("publish_dir", game.PublishDirectory);
        writer.WriteStartArray("mechanism_types");
        foreach (string module in MechanismTypes.Modules)
        {
            writer.WriteStringValue(module);
        }
        writer.WriteEndArray();
        writer.WriteEndObject();
    }

    private static void WriteGameplay(Utf8JsonWriter writer, ScadPlayGame game)
    {
        World? world = game.World;
        writer.WriteStartObject("gameplay");
        if (world == null)
        {
            writer.WriteEndObject();
            return;
        }
        writer.WriteString("title", world.LevelTitle);
        writer.WriteString("goal", world.LevelGoal);
        writer.WriteString("state", world.State switch
        {
            PlayState.Dead => "dead",
            PlayState.Won => "won",
            _ => "playing",
        });
        writer.WriteNumber("time", Round(world.LevelTime, 1));
        writer.WriteNumber("coins", world.Coins);
        writer.WriteNumber("coins_total", world.CoinsTotal);
        writer.WriteNumber("deaths", world.Deaths);
        writer.WriteString("checkpoint", world.CheckpointKey);

        writer.WriteStartArray("mechanisms");
        foreach (Mechanism mechanism in world.Mechanisms)
        {
            writer.WriteStartObject();
            writer.WriteString("key", mechanism.Key);
            writer.WriteString("module", mechanism.Module);
            writer.WriteBoolean("active", mechanism.Active);
            writer.WriteString("status", mechanism.Fault == null ? SafeDescribe(mechanism) : "faulted");
            WriteVector(writer, "pos", ScadSpace.ToScad(mechanism.Position));
            writer.WriteNumber("line", mechanism.Node.SourceLine);
            if (mechanism.On.Length > 0)
            {
                WriteStrings(writer, "on", mechanism.On);
            }
            if (mechanism.Any.Length > 0)
            {
                WriteStrings(writer, "any", mechanism.Any);
            }
            if (mechanism.Fault != null)
            {
                writer.WriteString("fault", mechanism.Fault);
            }
            writer.WriteEndObject();
        }
        writer.WriteEndArray();

        WriteStrings(writer, "unbound", world.Unbound);
        WriteStrings(writer, "warnings", world.Warnings);
        writer.WriteStartArray("events");
        foreach ((float time, string text) in world.Events)
        {
            writer.WriteStartObject();
            writer.WriteNumber("t", Round(time, 1));
            writer.WriteString("text", text);
            writer.WriteEndObject();
        }
        writer.WriteEndArray();
        writer.WriteEndObject();
    }

    private static string SafeDescribe(Mechanism mechanism)
    {
        try
        {
            return mechanism.Describe();
        }
        catch (Exception exception)
        {
            return $"Describe threw {exception.GetType().Name}";
        }
    }

    private static void WriteStrings(Utf8JsonWriter writer, string name, IEnumerable<string> values)
    {
        writer.WriteStartArray(name);
        foreach (string value in values)
        {
            writer.WriteStringValue(value);
        }
        writer.WriteEndArray();
    }

    private static void WriteVector(Utf8JsonWriter writer, string name, Vec3 value, int components = 3, int digits = 2)
    {
        writer.WriteStartArray(name);
        writer.WriteNumberValue(Round(value.X, digits));
        writer.WriteNumberValue(Round(value.Y, digits));
        if (components > 2)
        {
            writer.WriteNumberValue(Round(value.Z, digits));
        }
        writer.WriteEndArray();
    }

    /// <summary>Rounded so a player standing still does not rewrite the file every frame.</summary>
    private static double Round(float value, int digits = 2)
    {
        double rounded = Math.Round(value, digits);
        return rounded == 0.0 ? 0.0 : rounded;
    }
}
