using GkNext.Interop;

namespace GkNext;

/// <summary>
/// The colours the built-in HUD helpers and the game templates draw with, aligned with
/// <c>NextUI::Foundation::EColor</c> in <c>UiTheme.cpp</c>.
/// </summary>
/// <remarks>
/// Named roles rather than a palette to admire: <c>Danger</c> says what the colour means, so a
/// game that restyles its HUD changes one file instead of hunting for the particular shade of red
/// it typed into forty draw calls. Replace these values, or stop using the type — nothing in the
/// engine reads them except <see cref="ManagedImGui"/>.
/// </remarks>
public static class HudPalette
{
    /// <summary>Ordinary readable text.</summary>
    public static Color Text => Color.FromBytes(232, 237, 245);

    /// <summary>Secondary text: counters, coordinates, anything glanced at rather than read.</summary>
    public static Color Muted => Color.FromBytes(148, 156, 168);

    /// <summary>Dimmed/disabled text.</summary>
    public static Color TextDim => Color.FromBytes(110, 117, 128);

    /// <summary>Primary interactive accent (editor modern blue).</summary>
    public static Color Accent => Color.FromBytes(61, 143, 245);

    /// <summary>Hovered accent state.</summary>
    public static Color AccentHover => Color.FromBytes(82, 160, 252);

    /// <summary>Titles, gold badges, and the one number that matters.</summary>
    public static Color Highlight => Color.FromBytes(240, 171, 46);

    /// <summary>Positive status or ready prompt.</summary>
    public static Color Success => Color.FromBytes(56, 199, 107);

    /// <summary>Caution / warning status.</summary>
    public static Color Warning => Color.FromBytes(240, 171, 46);

    /// <summary>Death, damage, an empty magazine.</summary>
    public static Color Danger => Color.FromBytes(230, 74, 71);

    /// <summary>Standard floating HUD panel background.</summary>
    public static Color PanelBg => Color.FromBytes(30, 32, 35, 240);

    /// <summary>Elevated card or child container surface.</summary>
    public static Color SurfaceElevated => Color.FromBytes(37, 39, 43, 245);

    /// <summary>Subtle panel/card border.</summary>
    public static Color Border => Color.FromBytes(56, 60, 66, 216);

    /// <summary>Default button fill.</summary>
    public static Color ButtonBg => Color.FromBytes(42, 45, 50, 245);

    /// <summary>Hovered button fill.</summary>
    public static Color ButtonHover => Color.FromBytes(54, 58, 65, 250);

    /// <summary>Active/pressed button fill.</summary>
    public static Color ButtonActive => Color.FromBytes(61, 143, 245, 235);

    /// <summary>Disabled button fill.</summary>
    public static Color ButtonDisabled => Color.FromBytes(33, 35, 39, 190);

    /// <summary>The offset copy drawn under HUD text so it survives a bright background.</summary>
    public static Color Shadow => Color.FromBytes(8, 10, 14, 180);

    /// <summary>A resource bar that is doing fine.</summary>
    public static Color BarHealthy => Success;

    /// <summary>A resource bar that is not.</summary>
    public static Color BarCritical => Danger;

    /// <summary>
    /// Bar colour for a fraction in [0, 1], turning critical near the bottom.
    /// </summary>
    /// <remarks>
    /// A threshold rather than a gradient: a bar that fades continuously never tells the player
    /// the moment they are in trouble, and that moment is the only thing the colour is for.
    /// </remarks>
    public static Color Bar(float fraction, float criticalBelow = 0.35f)
        => fraction > criticalBelow ? BarHealthy : BarCritical;
}
