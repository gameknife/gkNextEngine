using System.Diagnostics;
using System.Text.Json;
using GkNext;

namespace ScadPlay;

/// <summary>
/// The level file, kept in step with the disk: every save of it -- or of any file it uses --
/// rebuilds the level in place.
/// </summary>
/// <remarks>
/// <para>Where it loads from matters. The runtime asset tree next to the executable is a build-time
/// copy; edits land in the source tree. So when this binary was built from a checkout that still
/// has the level, the absolute source path is loaded and watched, and the use/include closure
/// resolves against the source kits too -- editing kit_scadplay.scad is as live as editing the
/// level. Without a source tree (a packaged build) the runtime copy is loaded and nothing is live.</para>
///
/// <para>A save is checked before it is loaded. The engine drops the current scene before the new
/// one is parsed, so loading a file with a syntax error would leave the player in an empty world;
/// instead the error is reported and the last good level stays. Evaluation warnings ("unknown
/// module") do not block a build; they come back in the load report.</para>
///
/// <para><see cref="Serial"/> counts finished builds, failed ones included -- it is what an agent
/// waits on after an edit.</para>
/// </remarks>
public sealed class LiveLevel
{
    private const float PollSeconds = 0.25f;
    private const float DebounceSeconds = 0.15f;

    private readonly Dictionary<string, (DateTime Time, long Length)> stamps = new(StringComparer.Ordinal);
    private readonly List<string> watched = [];
    private float pollTimer;
    private float debounceTimer = -1.0f;
    private string pendingTrigger = string.Empty;
    private ulong loadReportSerialBefore;
    private readonly Stopwatch buildClock = new();

    /// <summary>What the level was asked to be, as an asset path or an absolute path.</summary>
    public string Requested { get; private set; } = string.Empty;

    /// <summary>The exact path handed to the engine, and the key of its load report.</summary>
    public string LoadPath { get; private set; } = string.Empty;

    /// <summary>True when <see cref="LoadPath"/> is a source-tree file that edits reach.</summary>
    public bool IsLive { get; private set; }

    public string LevelName => Path.GetFileNameWithoutExtension(LoadPath);

    public IReadOnlyList<string> Watched => watched;

    // --- build status, as the agent context reports it ----------------------------------------
    public int Serial { get; set; }
    public bool Building { get; private set; }
    public bool Ok { get; private set; } = true;
    public string Error { get; private set; } = string.Empty;
    public List<string> Warnings { get; private set; } = [];
    public int WarningCount { get; private set; }
    public int Nodes { get; private set; }
    public long Triangles { get; private set; }
    /// <summary>Save-to-playable: check, evaluate, upload, physics -- what the player waits for.</summary>
    public double Milliseconds { get; private set; }

    /// <summary>The SCAD evaluation alone, from the loader's report.</summary>
    public double EvaluateMilliseconds { get; private set; }
    public string Trigger { get; private set; } = string.Empty;
    public DateTime FinishedAt { get; private set; }

    /// <summary>True once a level has loaded at least once: a failed first build has nothing to show.</summary>
    public bool HasScene { get; private set; }

    /// <summary>Raised after every finished build, successful or not.</summary>
    public event Action? Finished;

    /// <summary>
    /// Points at a level and builds it. <paramref name="level"/> is an asset path
    /// ("assets/scad/source/scadplay/playground.scad"), an absolute path, or a bare level name
    /// resolved next to the current level.
    /// </summary>
    public void Open(string level, string projectRoot, string trigger)
    {
        Requested = level;
        LoadPath = Resolve(level, projectRoot);
        IsLive = Path.IsPathRooted(LoadPath) && File.Exists(LoadPath);
        watched.Clear();
        stamps.Clear();
        Rebuild(trigger);
    }

    private string Resolve(string level, string projectRoot)
    {
        if (!level.EndsWith(".scad", StringComparison.OrdinalIgnoreCase))
        {
            // A level name, as portals and goals write it: a file beside the current level.
            string directory = LoadPath.Length > 0 ? Path.GetDirectoryName(LoadPath) ?? string.Empty : string.Empty;
            level = directory.Length > 0 ? Path.Combine(directory, level + ".scad") : level + ".scad";
        }
        if (Path.IsPathRooted(level))
        {
            return Path.GetFullPath(level);
        }
        if (projectRoot.Length > 0)
        {
            string source = Path.GetFullPath(Path.Combine(projectRoot, level));
            if (File.Exists(source))
            {
                return source;
            }
        }
        return level.Replace('\\', '/');
    }

    /// <summary>Checks the file and, if it parses, asks the engine to rebuild the level from it.</summary>
    public bool Rebuild(string trigger)
    {
        Trigger = trigger;
        debounceTimer = -1.0f;
        buildClock.Restart();
        if (Building)
        {
            // Picked up when the build in flight finishes: its stamps are older than this save.
            return false;
        }

        bool parses = Assets.CheckSceneSource(LoadPath);
        SceneReport check = SceneReport.Parse(Assets.GetSceneCheckReport());
        Watch(check.Dependencies);
        if (!parses)
        {
            Finish(false, check.Error.Length > 0 ? check.Error : $"cannot read {LoadPath}", check);
            return false;
        }

        loadReportSerialBefore = SceneReport.Parse(Assets.GetSceneLoadReport(LoadPath)).Serial;
        Building = true;
        Engine.RequestLoadScene(LoadPath);
        return true;
    }

    /// <summary>Call from the game's scene-loaded hook.</summary>
    public void OnSceneLoaded()
    {
        if (!Building)
        {
            return;
        }
        SceneReport report = SceneReport.Parse(Assets.GetSceneLoadReport(LoadPath));
        Watch(report.Dependencies);
        HasScene = true;
        Finish(report.Ok, report.Error, report);
    }

    public void Tick(float deltaSeconds)
    {
        pollTimer += deltaSeconds;
        if (pollTimer < PollSeconds && debounceTimer < 0.0f)
        {
            return;
        }

        if (Building)
        {
            if (pollTimer >= PollSeconds)
            {
                pollTimer = 0.0f;
                // A load that fails after the check passed (a file vanished mid-build) never
                // reaches the scene-loaded hook; its report is the only sign it finished.
                SceneReport report = SceneReport.Parse(Assets.GetSceneLoadReport(LoadPath));
                if (report.Serial != loadReportSerialBefore && !report.Ok)
                {
                    HasScene = false;
                    Finish(false, report.Error, report);
                }
            }
            return;
        }

        if (pollTimer >= PollSeconds)
        {
            pollTimer = 0.0f;
            string? changed = FirstChangedFile();
            if (changed != null)
            {
                pendingTrigger = $"file:{Path.GetFileName(changed)}";
                debounceTimer = DebounceSeconds;
            }
        }

        if (debounceTimer >= 0.0f)
        {
            // Editors and agents often write a file in more than one go; wait for it to settle.
            debounceTimer -= deltaSeconds;
            if (debounceTimer < 0.0f)
            {
                Rebuild(pendingTrigger);
            }
        }
    }

    private void Finish(bool ok, string error, SceneReport report)
    {
        Building = false;
        Ok = ok;
        Error = ok ? string.Empty : error;
        Warnings = report.Warnings;
        WarningCount = report.WarningCount;
        if (ok)
        {
            Nodes = report.Nodes;
            Triangles = report.Triangles;
        }
        Milliseconds = buildClock.Elapsed.TotalMilliseconds;
        EvaluateMilliseconds = report.Milliseconds;
        FinishedAt = DateTime.UtcNow;
        Serial++;
        if (ok)
        {
            Log.Info($"[ScadPlay] build #{Serial} ok ({Trigger}): {Nodes} nodes, {Triangles} triangles, {WarningCount} warnings");
        }
        else
        {
            Log.Warn($"[ScadPlay] build #{Serial} failed ({Trigger}): {Error}");
        }
        Finished?.Invoke();

        // Saved again while that build ran: its stamps moved past the ones it was built from.
        if (FirstChangedFile() is string changed)
        {
            pendingTrigger = $"file:{Path.GetFileName(changed)}";
            debounceTimer = DebounceSeconds;
        }
    }

    private void Watch(List<string> files)
    {
        if (files.Count == 0)
        {
            return;
        }
        watched.Clear();
        stamps.Clear();
        foreach (string file in files)
        {
            if (Path.IsPathRooted(file) && !watched.Contains(file))
            {
                watched.Add(file);
                stamps[file] = Stamp(file);
            }
        }
    }

    private string? FirstChangedFile()
    {
        foreach (string file in watched)
        {
            if (stamps.TryGetValue(file, out (DateTime Time, long Length) known) && known != Stamp(file))
            {
                return file;
            }
        }
        return null;
    }

    /// <summary>Time and size, because some file systems store modification times to the second.</summary>
    private static (DateTime, long) Stamp(string file)
    {
        try
        {
            FileInfo info = new(file);
            return info.Exists ? (info.LastWriteTimeUtc, info.Length) : (DateTime.MinValue, -1);
        }
        catch (IOException)
        {
            return (DateTime.MinValue, -1);
        }
        catch (UnauthorizedAccessException)
        {
            return (DateTime.MinValue, -1);
        }
    }
}

/// <summary>The engine's JSON report on a scene check or load.</summary>
public sealed class SceneReport
{
    public bool Ok;
    public string Error = string.Empty;
    public List<string> Warnings = [];
    public int WarningCount;
    public List<string> Dependencies = [];
    public int Nodes;
    public long Triangles;
    public double Milliseconds;
    public ulong Serial;

    public static SceneReport Parse(string json)
    {
        SceneReport report = new();
        if (string.IsNullOrEmpty(json))
        {
            return report;
        }
        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            JsonElement root = document.RootElement;
            report.Ok = root.TryGetProperty("ok", out JsonElement ok) && ok.ValueKind == JsonValueKind.True;
            report.Error = root.TryGetProperty("error", out JsonElement error) ? error.GetString() ?? string.Empty : string.Empty;
            report.WarningCount = root.TryGetProperty("warning_count", out JsonElement count) ? count.GetInt32() : 0;
            report.Nodes = root.TryGetProperty("nodes", out JsonElement nodes) ? nodes.GetInt32() : 0;
            report.Triangles = root.TryGetProperty("triangles", out JsonElement triangles) ? triangles.GetInt64() : 0;
            report.Milliseconds = root.TryGetProperty("ms", out JsonElement ms) ? ms.GetDouble() : 0.0;
            report.Serial = root.TryGetProperty("serial", out JsonElement serial) ? serial.GetUInt64() : 0;
            if (root.TryGetProperty("warnings", out JsonElement warnings) && warnings.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement warning in warnings.EnumerateArray())
                {
                    report.Warnings.Add(warning.GetString() ?? string.Empty);
                }
            }
            if (root.TryGetProperty("dependencies", out JsonElement dependencies) && dependencies.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement dependency in dependencies.EnumerateArray())
                {
                    report.Dependencies.Add(dependency.GetString() ?? string.Empty);
                }
            }
        }
        catch (JsonException exception)
        {
            report.Error = $"unreadable scene report: {exception.Message}";
        }
        return report;
    }
}
