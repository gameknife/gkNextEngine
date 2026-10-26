using System.Text;
using GkNext.Interop;

namespace GkNext;

/// <summary>A pixel-space rectangle used by the managed immediate-mode UI.</summary>
public readonly struct UiRect(float x, float y, float width, float height)
{
    public readonly float X = x;
    public readonly float Y = y;
    public readonly float Width = width;
    public readonly float Height = height;

    public float Right => X + Width;
    public float Bottom => Y + Height;

    public bool Contains(Vector2 point)
        => point.X >= X && point.X <= Right && point.Y >= Y && point.Y <= Bottom;
}

/// <summary>
/// A C#-owned draw list. Commands, variable point data and UTF-8 text are accumulated in reusable
/// arrays and cross the ABI once in <see cref="Submit"/>.
/// </summary>
public sealed class ManagedDrawList
{
    private UiDrawCommand[] commands = new UiDrawCommand[256];
    private Vector2[] points = new Vector2[256];
    private byte[] utf8 = new byte[4096];
    private int commandCount;
    private int pointCount;
    private int utf8Count;

    public int Count => commandCount;

    public void Reset()
    {
        commandCount = 0;
        pointCount = 0;
        utf8Count = 0;
    }

    public void Submit()
    {
        UI.SubmitDrawList(commands.AsSpan(0, commandCount),
                          points.AsSpan(0, pointCount),
                          utf8.AsSpan(0, utf8Count));
    }

    public void AddLine(Vector2 from, Vector2 to, Color color, float thickness = 1.0f,
                        UiDrawLayer layer = UiDrawLayer.Foreground)
    {
        ref UiDrawCommand command = ref AddCommand(UiDrawCommandType.Line, color, layer);
        command.P0 = from;
        command.P1 = to;
        command.Thickness = thickness;
    }

    public void AddRect(UiRect rect, Color color, float rounding = 0.0f, float thickness = 1.0f,
                        UiDrawLayer layer = UiDrawLayer.Foreground)
    {
        ref UiDrawCommand command = ref AddCommand(UiDrawCommandType.Rect, color, layer);
        SetRect(ref command, rect);
        command.Rounding = rounding;
        command.Thickness = thickness;
    }

    public void AddRectFilled(UiRect rect, Color color, float rounding = 0.0f,
                              UiDrawLayer layer = UiDrawLayer.Foreground)
    {
        ref UiDrawCommand command = ref AddCommand(UiDrawCommandType.RectFilled, color, layer);
        SetRect(ref command, rect);
        command.Rounding = rounding;
    }

    public void AddCircle(Vector2 center, float radius, Color color, float thickness = 1.0f,
                          int segments = 0, UiDrawLayer layer = UiDrawLayer.Foreground)
    {
        ref UiDrawCommand command = ref AddCommand(UiDrawCommandType.Circle, color, layer);
        command.P0 = center;
        command.Radius = radius;
        command.Thickness = thickness;
        command.Flags = segments;
    }

    public void AddCircleFilled(Vector2 center, float radius, Color color, int segments = 0,
                                UiDrawLayer layer = UiDrawLayer.Foreground)
    {
        ref UiDrawCommand command = ref AddCommand(UiDrawCommandType.CircleFilled, color, layer);
        command.P0 = center;
        command.Radius = radius;
        command.Flags = segments;
    }

    public void AddPolyline(ReadOnlySpan<Vector2> vertices, Color color, float thickness = 1.0f,
                            bool closed = false, UiDrawLayer layer = UiDrawLayer.Foreground)
    {
        if (vertices.Length < 2)
        {
            return;
        }
        int offset = AppendPoints(vertices);
        ref UiDrawCommand command = ref AddCommand(UiDrawCommandType.Polyline, color, layer);
        command.DataOffset = offset;
        command.DataCount = vertices.Length;
        command.Thickness = thickness;
        command.Flags = closed ? 1 : 0;
    }

    public void AddConvexPolyFilled(ReadOnlySpan<Vector2> vertices, Color color,
                                    UiDrawLayer layer = UiDrawLayer.Foreground)
    {
        if (vertices.Length < 3)
        {
            return;
        }
        int offset = AppendPoints(vertices);
        ref UiDrawCommand command = ref AddCommand(UiDrawCommandType.ConvexPolyFilled, color, layer);
        command.DataOffset = offset;
        command.DataCount = vertices.Length;
    }

    public void AddText(string text, Vector2 position, Color color, float scale = 1.0f,
                        UiDrawLayer layer = UiDrawLayer.Foreground)
    {
        if (string.IsNullOrEmpty(text))
        {
            return;
        }
        int byteCount = Encoding.UTF8.GetByteCount(text);
        EnsureUtf8Capacity(utf8Count + byteCount);
        int written = Encoding.UTF8.GetBytes(text.AsSpan(), utf8.AsSpan(utf8Count, byteCount));
        ref UiDrawCommand command = ref AddCommand(UiDrawCommandType.Text, color, layer);
        command.P0 = position;
        command.Scale = scale;
        command.DataOffset = utf8Count;
        command.DataCount = written;
        utf8Count += written;
    }

    public void AddImage(UiTexture texture, UiRect rect, Color tint,
                         Vector2 uv0 = default, Vector2 uv1 = default,
                         UiDrawLayer layer = UiDrawLayer.Foreground)
    {
        if (!texture.IsValid)
        {
            return;
        }
        if (uv1.X == 0.0f && uv1.Y == 0.0f)
        {
            uv1 = new Vector2(1.0f, 1.0f);
        }
        ref UiDrawCommand command = ref AddCommand(UiDrawCommandType.Image, tint, layer);
        SetRect(ref command, rect);
        command.TextureHandle = texture.Handle;
        command.Uv0 = uv0;
        command.Uv1 = uv1;
    }

    private ref UiDrawCommand AddCommand(UiDrawCommandType type, Color color, UiDrawLayer layer)
    {
        EnsureCommandCapacity(commandCount + 1);
        ref UiDrawCommand command = ref commands[commandCount++];
        command = default;
        command.Type = type;
        command.Layer = layer;
        command.Color = color.ToPacked();
        command.Thickness = 1.0f;
        command.Scale = 1.0f;
        return ref command;
    }

    private int AppendPoints(ReadOnlySpan<Vector2> vertices)
    {
        int offset = pointCount;
        EnsurePointCapacity(pointCount + vertices.Length);
        vertices.CopyTo(points.AsSpan(pointCount));
        pointCount += vertices.Length;
        return offset;
    }

    private static void SetRect(ref UiDrawCommand command, UiRect rect)
    {
        command.P0 = new Vector2(rect.X, rect.Y);
        command.P1 = new Vector2(rect.Right, rect.Bottom);
    }

    private void EnsureCommandCapacity(int required)
    {
        if (required > commands.Length)
        {
            Array.Resize(ref commands, Math.Max(required, commands.Length * 2));
        }
    }

    private void EnsurePointCapacity(int required)
    {
        if (required > points.Length)
        {
            Array.Resize(ref points, Math.Max(required, points.Length * 2));
        }
    }

    private void EnsureUtf8Capacity(int required)
    {
        if (required > utf8.Length)
        {
            Array.Resize(ref utf8, Math.Max(required, utf8.Length * 2));
        }
    }
}

/// <summary>
/// Small immediate-mode widget layer implemented entirely in C#. It owns hit testing and visual
/// state; native ImGui is used only as the final primitive renderer through ManagedDrawList.
/// </summary>
public sealed class ManagedImGui
{
    private readonly ManagedDrawList drawList = new();
    private Vector2 mousePosition;
    private bool mouseDown;
    private bool mousePressed;
    private int hotId;

    public ManagedDrawList DrawList => drawList;
    public Vector2 ScreenSize { get; private set; }
    public Vector2 MousePosition => mousePosition;
    public int HotId => hotId;

    public void BeginFrame()
    {
        drawList.Reset();
        ScreenSize = UI.GetScreenSize();
        mousePosition = Input.GetMousePosition();
        // The runtime clears its per-tick "pressed" set after managed gameplay has observed it,
        // while UI is rendered later in the frame. Deriving the edge from the persistent down
        // state keeps C#-owned widgets independent of that ordering; the explicit pressed query
        // still catches a very short click that begins and ends between two UI frames.
        bool wasMouseDown = mouseDown;
        mouseDown = Input.IsMouseButtonDown(1);
        mousePressed = Input.IsMouseButtonPressed(1) || (mouseDown && !wasMouseDown);
        hotId = 0;
    }

    public void EndFrame() => drawList.Submit();

    public void Panel(UiRect rect, float rounding = 7.0f)
    {
        drawList.AddRectFilled(new UiRect(rect.X + 4.0f, rect.Y + 6.0f, rect.Width, rect.Height),
                               Color.FromBytes(0, 0, 0, 72), rounding);
        drawList.AddRectFilled(rect, HudPalette.PanelBg, rounding);
        drawList.AddRect(rect, HudPalette.Border, rounding, 1.0f);
    }

    public bool Button(int id, UiRect rect, string label, bool enabled = true,
                       float textScale = 1.0f)
    {
        bool hovered = enabled && rect.Contains(mousePosition);
        if (hovered)
        {
            hotId = id;
        }
        Color fill = !enabled ? HudPalette.ButtonDisabled : hovered ? HudPalette.ButtonHover : HudPalette.ButtonBg;
        if (hovered && mouseDown)
        {
            fill = HudPalette.ButtonActive;
        }
        drawList.AddRectFilled(rect, fill, 5.0f);
        drawList.AddRect(rect, enabled ? (hovered ? HudPalette.Accent : HudPalette.Border) : Color.FromBytes(56, 60, 66, 110),
                         5.0f, 1.0f);
        DrawTextCentered(label, rect, enabled ? HudPalette.Text : HudPalette.TextDim, textScale);
        return hovered && mousePressed;
    }

    public bool Checkbox(int id, UiRect rect, string label, ref bool value)
    {
        UiRect box = new(rect.X, rect.Y + Math.Max(0.0f, (rect.Height - 22.0f) * 0.5f), 22.0f, 22.0f);
        bool clicked = Button(id, box, value ? "✓" : string.Empty, true, 1.0f);
        if (clicked)
        {
            value = !value;
        }
        drawList.AddText(label, new Vector2(rect.X + 31.0f, rect.Y + (rect.Height - 16.0f) * 0.5f), HudPalette.Text);
        return clicked;
    }

    public bool SliderFloat(int id, UiRect rect, ref float value, float min, float max)
    {
        bool hovered = rect.Contains(mousePosition);
        bool changed = false;
        if (hovered)
        {
            hotId = id;
        }
        if (hovered && mouseDown && max > min)
        {
            float next = min + Math.Clamp((mousePosition.X - rect.X) / Math.Max(1.0f, rect.Width), 0.0f, 1.0f) * (max - min);
            changed = Math.Abs(next - value) > 0.0001f;
            value = next;
        }
        float ratio = max > min ? Math.Clamp((value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
        drawList.AddRectFilled(rect, Color.FromBytes(20, 22, 25, 235), rect.Height * 0.5f);
        drawList.AddRectFilled(new UiRect(rect.X, rect.Y, rect.Width * ratio, rect.Height),
                               HudPalette.Accent, rect.Height * 0.5f);
        drawList.AddCircleFilled(new Vector2(rect.X + rect.Width * ratio, rect.Y + rect.Height * 0.5f),
                                 rect.Height * 0.65f, hovered ? Color.White : HudPalette.Text);
        return changed;
    }

    public void ProgressBar(UiRect rect, float ratio, Color fill, string? label = null)
    {
        ratio = Math.Clamp(ratio, 0.0f, 1.0f);
        float rounding = Math.Min(4.0f, rect.Height * 0.35f);
        drawList.AddRectFilled(rect, Color.FromBytes(18, 20, 23, 225), rounding);
        if (ratio > 0.0f)
        {
            drawList.AddRectFilled(new UiRect(rect.X, rect.Y, rect.Width * ratio, rect.Height),
                                   fill, rounding);
        }
        drawList.AddRect(rect, HudPalette.Border, rounding, 1.0f);
        if (!string.IsNullOrEmpty(label))
        {
            DrawTextCentered(label, rect, HudPalette.Text, 0.9f);
        }
    }

    public void DrawTextCentered(string text, UiRect rect, Color color, float scale = 1.0f)
    {
        Vector2 size = UI.CalcTextSize(text, scale);
        drawList.AddText(text,
                         new Vector2(rect.X + (rect.Width - size.X) * 0.5f,
                                     rect.Y + (rect.Height - size.Y) * 0.5f),
                         color,
                         scale);
    }

    public void DrawTextCenteredX(string text, float y, Color color, float scale = 1.0f,
                                  bool shadow = false)
    {
        Vector2 size = UI.CalcTextSize(text, scale);
        DrawText(text, (ScreenSize.X - size.X) * 0.5f, y, color, scale, shadow);
    }

    /// <summary>
    /// HUD text at a pixel position, optionally with a drop shadow.
    /// </summary>
    /// <remarks>
    /// The shadow is worth asking for on anything drawn over the scene rather than over a panel.
    /// A HUD sits on whatever the camera happens to be looking at, and pale text on a pale wall is
    /// simply not there; one dark copy offset by a pixel costs nothing and always reads.
    /// </remarks>
    public void DrawText(string text, float x, float y, Color color, float scale = 1.0f,
                         bool shadow = false)
    {
        if (shadow)
        {
            drawList.AddText(text, new Vector2(x + 1.0f, y + 1.0f), HudPalette.Shadow, scale);
        }
        drawList.AddText(text, new Vector2(x, y), color, scale);
    }

    /// <summary>Text pinned to the bottom right, where a control hint or a build stamp goes.</summary>
    public void DrawTextBottomRight(string text, float margin, Color color, float scale = 1.0f,
                                    bool shadow = true)
    {
        Vector2 size = UI.CalcTextSize(text, scale);
        DrawText(text, ScreenSize.X - size.X - margin, ScreenSize.Y - size.Y - margin, color, scale,
                 shadow);
    }

    /// <summary>
    /// A panel centred horizontally, returning the rectangle it occupies so its contents can be
    /// laid out against it.
    /// </summary>
    public UiRect PanelCenteredX(float width, float y, float height, float rounding = 7.0f)
    {
        UiRect rect = new((ScreenSize.X - width) * 0.5f, y, width, height);
        Panel(rect, rounding);
        return rect;
    }

    /// <summary>
    /// Draws a full-screen dim backdrop and a centered modal card, returning the card's rectangle.
    /// </summary>
    public UiRect ModalCardCentered(float width, float height, float rounding = 7.0f, byte dimAlpha = 155)
    {
        if (dimAlpha > 0)
        {
            drawList.AddRectFilled(new UiRect(0.0f, 0.0f, ScreenSize.X, ScreenSize.Y),
                                   Color.FromBytes(4, 6, 9, dimAlpha), 0.0f, UiDrawLayer.Background);
        }
        UiRect rect = new((ScreenSize.X - width) * 0.5f, (ScreenSize.Y - height) * 0.5f, width, height);
        Panel(rect, rounding);
        return rect;
    }

    /// <summary>
    /// Draws a 4-way tactical crosshair with a center dot.
    /// </summary>
    public void DrawCrosshair(Vector2 center, float gap = 5.0f, float length = 8.0f,
                              float thickness = 1.5f, Color? color = null)
    {
        Color stroke = color ?? HudPalette.Text;
        drawList.AddLine(new Vector2(center.X - gap - length, center.Y), new Vector2(center.X - gap, center.Y), stroke, thickness);
        drawList.AddLine(new Vector2(center.X + gap, center.Y), new Vector2(center.X + gap + length, center.Y), stroke, thickness);
        drawList.AddLine(new Vector2(center.X, center.Y - gap - length), new Vector2(center.X, center.Y - gap), stroke, thickness);
        drawList.AddLine(new Vector2(center.X, center.Y + gap), new Vector2(center.X, center.Y + gap + length), stroke, thickness);
        drawList.AddCircleFilled(center, 1.5f, stroke);
    }

    /// <summary>
    /// Draws a key-cap pill badge followed by an action label, returning the total width advanced.
    /// </summary>
    public float DrawKeyHintPill(Vector2 pos, string keyLabel, string actionText, float scale = 1.0f)
    {
        Vector2 keySize = UI.CalcTextSize(keyLabel, scale);
        float padX = 7.0f * scale;
        float padY = 3.0f * scale;
        UiRect pillRect = new(pos.X, pos.Y, keySize.X + padX * 2.0f, keySize.Y + padY * 2.0f);
        drawList.AddRectFilled(pillRect, HudPalette.SurfaceElevated, 4.0f);
        drawList.AddRect(pillRect, HudPalette.Border, 4.0f, 1.0f);
        drawList.AddText(keyLabel, new Vector2(pos.X + padX, pos.Y + padY), HudPalette.Highlight, scale);

        if (string.IsNullOrEmpty(actionText))
        {
            return pillRect.Width;
        }

        float gap = 6.0f * scale;
        Vector2 actionSize = UI.CalcTextSize(actionText, scale);
        DrawText(actionText, pillRect.Right + gap, pos.Y + padY, HudPalette.Muted, scale, shadow: true);
        return pillRect.Width + gap + actionSize.X;
    }
}

/// <summary>Path-keyed texture cache; invalid asynchronous requests are retried on later frames.</summary>
public sealed class ManagedTextureCache
{
    private readonly Dictionary<string, UiTexture> textures = new(StringComparer.Ordinal);

    public UiTexture Get(string path, bool srgb = true)
    {
        if (textures.TryGetValue(path, out UiTexture texture) && texture.IsValid)
        {
            return texture;
        }
        texture = UI.RequestTexture(path, srgb, persistent: true);
        textures[path] = texture;
        return texture;
    }

    public void Clear() => textures.Clear();
}
