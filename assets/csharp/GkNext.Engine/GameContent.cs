using GkNext.Interop;

namespace GkNext;

/// <summary>
/// The running game project's own content: everything in <c>projects/&lt;Game&gt;/Content/</c>.
/// </summary>
/// <remarks>
/// A game project keeps its configs, sounds and scenes in its <c>Content/</c> folder, beside its
/// manifest and its <c>Scripts/</c>. At runtime that folder is part of the engine's asset tree
/// (<c>assets/projects/&lt;Game&gt;/Content</c>), so every path-taking API — <c>Assets.ReadFile</c>,
/// <c>Audio.PlaySfx</c>, <c>Engine.RequestLoadScene</c>, <c>UI.RequestTexture</c> — reaches it through an
/// ordinary asset path. This class builds that path from one relative to <c>Content/</c>, so game code
/// never spells out where its project ended up. Engine assets (<c>assets/scad/...</c>) are still named
/// by their asset path directly.
///
/// The root is asked for on every call rather than cached here: GkNext.Engine is never unloaded,
/// while the launcher swaps one game project for another underneath it. Cache the resolved paths in
/// the game instead — resolve them once in <c>OnInit</c>, not every frame.
/// </remarks>
public static class GameContent
{
    private static bool warnedNoProject;

    /// <summary>The <c>Content/</c> folder as an asset path, or empty when no game project is loaded.</summary>
    public static string Root => Assets.GetGameContentRoot();

    /// <summary>
    /// A path relative to the project's <c>Content/</c> folder, as a path every engine API accepts:
    /// <c>GameContent.Path("sounds/flap.wav")</c> is <c>assets/projects/Flappy/Content/sounds/flap.wav</c>.
    /// </summary>
    public static string Path(string relativePath)
    {
        string root = Root;
        if (root.Length == 0)
        {
            // Only happens when an assembly is loaded without a manifest behind it. Say so once:
            // every lookup that follows would otherwise fail without a word.
            if (!warnedNoProject)
            {
                warnedNoProject = true;
                Log.Warn($"GameContent.Path(\"{relativePath}\"): no game project is loaded, so there is no Content/ to resolve against");
            }
            return relativePath;
        }
        return root + "/" + relativePath.TrimStart('/');
    }

    /// <summary>Reads a file from the project's <c>Content/</c> folder. Empty when it does not exist.</summary>
    public static byte[] ReadFile(string relativePath) => Assets.ReadFile(Path(relativePath));
}
