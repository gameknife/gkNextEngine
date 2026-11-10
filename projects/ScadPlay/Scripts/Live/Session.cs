using System.Text.Json;
using GkNext;

namespace ScadPlay;

/// <summary>
/// What a hot-reloaded game instance inherits from the one it replaces.
/// </summary>
/// <remarks>
/// <para>A C# hot reload unloads the old assembly -- every field, every static -- and starts a new
/// instance. Gameplay rewritten by an agent mid-play should not cost the player their place, so the
/// outgoing instance writes this file in OnDestroy and the incoming one reads it in OnInit, then
/// rebuilds the level and puts everything back: the player where they stood, the camera where it
/// looked, every mechanism in the state it had reached.</para>
///
/// <para>The file is only honoured by the same process (a fresh launch starts fresh), and only for
/// a minute (a stale file from a crash is not a reload).</para>
/// </remarks>
public sealed class Session
{
    private const double MaxAgeSeconds = 60.0;

    public string Level = string.Empty;
    public Vec3 PlayerPosition;
    public Vec3 PlayerVelocity;
    public float PlayerYaw;
    public float CameraYaw;
    public float CameraPitch;
    public bool PointerLocked;
    public int Deaths;
    public float LevelTime;
    public string Checkpoint = string.Empty;
    public int ScriptSerial;
    public int BuildSerial;
    public Dictionary<string, StateBag> States = new(StringComparer.Ordinal);

    public static string PathFor(string projectRoot) =>
        System.IO.Path.Combine(projectRoot.Length > 0 ? projectRoot : Directory.GetCurrentDirectory(), "out", "scadplay", "session.json");

    public void Save(string file)
    {
        try
        {
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(file)!);
            using FileStream stream = File.Create(file);
            using Utf8JsonWriter writer = new(stream, new JsonWriterOptions { Indented = true });
            writer.WriteStartObject();
            writer.WriteNumber("pid", Environment.ProcessId);
            writer.WriteString("saved", DateTime.UtcNow.ToString("o"));
            writer.WriteString("level", Level);
            WriteVector(writer, "player_position", PlayerPosition);
            WriteVector(writer, "player_velocity", PlayerVelocity);
            writer.WriteNumber("player_yaw", PlayerYaw);
            writer.WriteNumber("camera_yaw", CameraYaw);
            writer.WriteNumber("camera_pitch", CameraPitch);
            writer.WriteBoolean("pointer_locked", PointerLocked);
            writer.WriteNumber("deaths", Deaths);
            writer.WriteNumber("level_time", LevelTime);
            writer.WriteString("checkpoint", Checkpoint);
            writer.WriteNumber("script_serial", ScriptSerial);
            writer.WriteNumber("build_serial", BuildSerial);
            writer.WriteStartObject("states");
            foreach ((string key, StateBag state) in States)
            {
                writer.WritePropertyName(key);
                state.Write(writer);
            }
            writer.WriteEndObject();
            writer.WriteEndObject();
        }
        catch (IOException exception)
        {
            Log.Warn($"[ScadPlay] could not save the reload session: {exception.Message}");
        }
        catch (UnauthorizedAccessException exception)
        {
            Log.Warn($"[ScadPlay] could not save the reload session: {exception.Message}");
        }
    }

    /// <summary>The session left by an instance of this same process moments ago, or null.</summary>
    public static Session? TryLoad(string file)
    {
        try
        {
            if (!File.Exists(file))
            {
                return null;
            }
            using JsonDocument document = JsonDocument.Parse(File.ReadAllBytes(file));
            JsonElement root = document.RootElement;
            if (root.GetProperty("pid").GetInt32() != Environment.ProcessId ||
                !DateTime.TryParse(root.GetProperty("saved").GetString(), null,
                                   System.Globalization.DateTimeStyles.RoundtripKind, out DateTime saved) ||
                (DateTime.UtcNow - saved).TotalSeconds > MaxAgeSeconds)
            {
                return null;
            }

            Session session = new()
            {
                Level = root.GetProperty("level").GetString() ?? string.Empty,
                PlayerPosition = ReadVector(root.GetProperty("player_position")),
                PlayerVelocity = ReadVector(root.GetProperty("player_velocity")),
                PlayerYaw = root.GetProperty("player_yaw").GetSingle(),
                CameraYaw = root.GetProperty("camera_yaw").GetSingle(),
                CameraPitch = root.GetProperty("camera_pitch").GetSingle(),
                PointerLocked = root.GetProperty("pointer_locked").GetBoolean(),
                Deaths = root.GetProperty("deaths").GetInt32(),
                LevelTime = root.GetProperty("level_time").GetSingle(),
                Checkpoint = root.GetProperty("checkpoint").GetString() ?? string.Empty,
                ScriptSerial = root.GetProperty("script_serial").GetInt32(),
                BuildSerial = root.GetProperty("build_serial").GetInt32(),
            };
            foreach (JsonProperty state in root.GetProperty("states").EnumerateObject())
            {
                session.States[state.Name] = StateBag.Read(state.Value);
            }
            return session;
        }
        catch (Exception exception) when (exception is IOException or JsonException or KeyNotFoundException or
                                              InvalidOperationException or FormatException)
        {
            Log.Warn($"[ScadPlay] ignoring an unreadable reload session: {exception.Message}");
            return null;
        }
    }

    private static void WriteVector(Utf8JsonWriter writer, string name, Vec3 value)
    {
        writer.WriteStartArray(name);
        writer.WriteNumberValue(value.X);
        writer.WriteNumberValue(value.Y);
        writer.WriteNumberValue(value.Z);
        writer.WriteEndArray();
    }

    private static Vec3 ReadVector(JsonElement element) =>
        new(element[0].GetSingle(), element[1].GetSingle(), element[2].GetSingle());
}
