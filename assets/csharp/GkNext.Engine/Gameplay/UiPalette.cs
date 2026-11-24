using GkNext.Interop;

namespace GkNext;

/// <summary>Semantic UI color tokens synchronized 1:1 with native <c>NextUI::EColor</c>.</summary>
public enum UiColorToken : int
{
    Text = 0,
    TextMuted = 1,
    TextDim = 2,
    Background = 3,
    Surface = 4,
    SurfaceElevated = 5,
    SurfaceHover = 6,
    Border = 7,
    BorderStrong = 8,
    Accent = 9,
    AccentHover = 10,
    Brand = 11,
    Success = 12,
    Warning = 13,
    Danger = 14,
    Blue = 15,
}

/// <summary>
/// Semantic design tokens for C# HUDs and menus, backed directly by native <c>NextUI::Color</c>
/// via <c>UI.GetThemeColor</c> so C++ and C# share a single source of truth.
/// </summary>
public static class UiPalette
{
    public static Color Token(UiColorToken token, float alpha = 1.0f)
    {
        if (Api.IsReady)
        {
            uint packed = UI.GetThemeColor((int)token, alpha);
            if (packed != 0)
            {
                return Color.FromPacked(packed);
            }
        }
        return Fallback(token, alpha);
    }

    public static Color Text => Token(UiColorToken.Text);
    public static Color Muted => Token(UiColorToken.TextMuted);
    public static Color TextDim => Token(UiColorToken.TextDim);

    public static Color Accent => Token(UiColorToken.Accent);
    public static Color AccentHover => Token(UiColorToken.AccentHover);
    public static Color Highlight => Token(UiColorToken.Brand);

    public static Color Success => Token(UiColorToken.Success);
    public static Color Warning => Token(UiColorToken.Warning);
    public static Color Danger => Token(UiColorToken.Danger);

    public static Color PanelBg => Token(UiColorToken.Surface, 0.94f);
    public static Color SurfaceElevated => Token(UiColorToken.SurfaceElevated, 0.96f);
    public static Color Border => Token(UiColorToken.Border, 0.85f);

    public static Color ButtonBg => Token(UiColorToken.SurfaceElevated, 0.96f);
    public static Color ButtonHover => Token(UiColorToken.SurfaceHover, 0.98f);
    public static Color ButtonActive => Token(UiColorToken.Accent, 0.92f);
    public static Color ButtonDisabled => Token(UiColorToken.Surface, 0.75f);

    public static Color Shadow => Color.FromBytes(8, 10, 14, 180);

    public static Color BarHealthy => Success;
    public static Color BarCritical => Danger;

    public static Color Bar(float fraction, float criticalBelow = 0.35f)
        => fraction > criticalBelow ? BarHealthy : BarCritical;

    private static Color Fallback(UiColorToken token, float alpha)
    {
        byte a = (byte)(Math.Clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return token switch
        {
            UiColorToken.Text => Color.FromBytes(232, 237, 245, a),
            UiColorToken.TextMuted => Color.FromBytes(148, 156, 168, a),
            UiColorToken.TextDim => Color.FromBytes(110, 117, 128, a),
            UiColorToken.Background => Color.FromBytes(20, 22, 25, a),
            UiColorToken.Surface => Color.FromBytes(30, 32, 35, a),
            UiColorToken.SurfaceElevated => Color.FromBytes(37, 39, 43, a),
            UiColorToken.SurfaceHover => Color.FromBytes(54, 58, 65, a),
            UiColorToken.Border => Color.FromBytes(56, 60, 66, a),
            UiColorToken.BorderStrong => Color.FromBytes(78, 84, 92, a),
            UiColorToken.Accent => Color.FromBytes(61, 143, 245, a),
            UiColorToken.AccentHover => Color.FromBytes(82, 160, 252, a),
            UiColorToken.Brand or UiColorToken.Warning => Color.FromBytes(240, 171, 46, a),
            UiColorToken.Success => Color.FromBytes(56, 199, 107, a),
            UiColorToken.Danger => Color.FromBytes(230, 74, 71, a),
            UiColorToken.Blue => Color.FromBytes(61, 143, 245, a),
            _ => Color.FromBytes(255, 255, 255, a),
        };
    }
}
