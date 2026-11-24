using System.Text;
using GkNext.Interop;

namespace GkNext;

/// <summary>9-way parent-relative anchor presets for zero-GC HUD and modal layout.</summary>
public enum UiAnchor : byte
{
    TopLeft = 0,
    TopCenter = 1,
    TopRight = 2,
    MiddleLeft = 3,
    Center = 4,
    MiddleRight = 5,
    BottomLeft = 6,
    BottomCenter = 7,
    BottomRight = 8,
}

/// <summary>A pixel-space rectangle used by the managed immediate-mode UI.</summary>
public readonly struct UiRect(float x, float y, float width, float height)
{
    public readonly float X = x;
    public readonly float Y = y;
    public readonly float Width = width;
    public readonly float Height = height;

    public float Right => X + Width;
    public float Bottom => Y + Height;
    public Vector2 Position => new(X, Y);
    public Vector2 Size => new(Width, Height);
    public Vector2 Center => new(X + Width * 0.5f, Y + Height * 0.5f);

    public bool Contains(Vector2 point)
        => point.X >= X && point.X <= Right && point.Y >= Y && point.Y <= Bottom;

    public static UiRect FromAnchor(Vector2 parentSize, UiAnchor anchor, Vector2 size, Vector2 offset = default)
    {
        float x = anchor switch
        {
            UiAnchor.TopLeft or UiAnchor.MiddleLeft or UiAnchor.BottomLeft => offset.X,
            UiAnchor.TopCenter or UiAnchor.Center or UiAnchor.BottomCenter => (parentSize.X - size.X) * 0.5f + offset.X,
            _ => parentSize.X - size.X + offset.X,
        };

        float y = anchor switch
        {
            UiAnchor.TopLeft or UiAnchor.TopCenter or UiAnchor.TopRight => offset.Y,
            UiAnchor.MiddleLeft or UiAnchor.Center or UiAnchor.MiddleRight => (parentSize.Y - size.Y) * 0.5f + offset.Y,
            _ => parentSize.Y - size.Y + offset.Y,
        };

        return new UiRect(x, y, size.X, size.Y);
    }

    public UiRect Inset(float padding)
        => Inset(padding, padding);

    public UiRect Inset(float padX, float padY)
        => new(X + padX, Y + padY, Math.Max(0.0f, Width - padX * 2.0f), Math.Max(0.0f, Height - padY * 2.0f));

    public UiRect SplitTop(float height, out UiRect remainder, float gap = 0.0f)
    {
        float clamped = Math.Clamp(height, 0.0f, Height);
        float nextY = Math.Min(Bottom, Y + clamped + gap);
        remainder = new UiRect(X, nextY, Width, Math.Max(0.0f, Bottom - nextY));
        return new UiRect(X, Y, Width, clamped);
    }

    public UiRect SplitBottom(float height, out UiRect remainder, float gap = 0.0f)
    {
        float clamped = Math.Clamp(height, 0.0f, Height);
        float splitY = Bottom - clamped;
        float remHeight = Math.Max(0.0f, splitY - gap - Y);
        remainder = new UiRect(X, Y, Width, remHeight);
        return new UiRect(X, splitY, Width, clamped);
    }

    public UiRect SplitLeft(float width, out UiRect remainder, float gap = 0.0f)
    {
        float clamped = Math.Clamp(width, 0.0f, Width);
        float nextX = Math.Min(Right, X + clamped + gap);
        remainder = new UiRect(nextX, Y, Math.Max(0.0f, Right - nextX), Height);
        return new UiRect(X, Y, clamped, Height);
    }

    public UiRect SplitRight(float width, out UiRect remainder, float gap = 0.0f)
    {
        float clamped = Math.Clamp(width, 0.0f, Width);
        float splitX = Right - clamped;
        float remWidth = Math.Max(0.0f, splitX - gap - X);
        remainder = new UiRect(X, Y, remWidth, Height);
        return new UiRect(splitX, Y, clamped, Height);
    }
}

/// <summary>
/// A C#-owned draw list. Commands, variable point data and UTF-8 text are accumulated in reusable
/// arrays and cross the ABI once in <see cref="Submit"/>.
/// </summary>
public sealed class UiDrawList
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
/// Immediate-mode 2D UI canvas implemented in C#. Owns hot/active interaction state, layout helpers,
/// input capture gating, and batched draw command submission to the engine's unified UI renderer.
/// </summary>
public sealed class UiCanvas
{
    private readonly UiDrawList drawList = new();
    private Vector2 mousePosition;
    private bool mouseDown;
    private bool mousePressed;
    private bool mouseReleased;
    private int hotId;
    private int activeId;
    private bool frameCapturedMouse;

    public UiDrawList DrawList => drawList;
    public Vector2 ScreenSize { get; private set; }
    public Vector2 MousePosition => mousePosition;
    public int HotId => hotId;
    public int ActiveId => activeId;
    public bool CapturesMouse => hotId != 0 || activeId != 0 || frameCapturedMouse;

    public void BeginFrame()
    {
        drawList.Reset();
        ScreenSize = UI.GetScreenSize();
        mousePosition = Input.GetMousePosition();

        bool wasMouseDown = mouseDown;
        mouseDown = Input.IsMouseButtonDown(1);
        mousePressed = Input.IsMouseButtonPressed(1) || (mouseDown && !wasMouseDown);
        mouseReleased = !mouseDown && wasMouseDown;

        hotId = 0;
        frameCapturedMouse = false;
    }

    public void EndFrame()
    {
        if (!mouseDown && !mousePressed)
        {
            activeId = 0;
        }
        UI.ClaimInputCapture(CapturesMouse, false);
        drawList.Submit();
    }

    /// <summary>Explicitly marks a screen rectangle as blocking scene mouse clicks when hovered.</summary>
    public void CaptureMouseRect(UiRect rect)
    {
        if (rect.Contains(mousePosition))
        {
            frameCapturedMouse = true;
        }
    }

    public void Panel(UiRect rect, float rounding = 7.0f, bool captureMouse = false)
    {
        if (captureMouse)
        {
            CaptureMouseRect(rect);
        }
        drawList.AddRectFilled(new UiRect(rect.X + 4.0f, rect.Y + 6.0f, rect.Width, rect.Height),
                               Color.FromBytes(0, 0, 0, 72), rounding);
        drawList.AddRectFilled(rect, UiPalette.PanelBg, rounding);
        drawList.AddRect(rect, UiPalette.Border, rounding, 1.0f);
    }

    public bool Button(int id, UiRect rect, string label, bool enabled = true, float textScale = 1.0f)
    {
        if (!enabled && activeId == id)
        {
            activeId = 0;
        }

        bool hovered = enabled && rect.Contains(mousePosition);
        if (hovered && (activeId == 0 || activeId == id))
        {
            hotId = id;
        }

        bool clicked = false;
        if (hovered && activeId == 0 && mousePressed)
        {
            activeId = id;
            // Short single-frame synthetic click (mousePressed without persistent mouseDown) triggers immediately.
            if (!mouseDown)
            {
                clicked = true;
                activeId = 0;
            }
        }
        else if (activeId == id && mouseReleased)
        {
            if (hovered)
            {
                clicked = true;
            }
            activeId = 0;
        }

        bool held = activeId == id && mouseDown;
        Color fill = !enabled
            ? UiPalette.ButtonDisabled
            : held && hovered
                ? UiPalette.ButtonActive
                : hovered
                    ? UiPalette.ButtonHover
                    : UiPalette.ButtonBg;

        Color border = !enabled
            ? Color.FromBytes(56, 60, 66, 110)
            : (hovered || held)
                ? UiPalette.Accent
                : UiPalette.Border;

        drawList.AddRectFilled(rect, fill, 5.0f);
        drawList.AddRect(rect, border, 5.0f, 1.0f);
        DrawTextCentered(label, rect, enabled ? UiPalette.Text : UiPalette.TextDim, textScale);
        return clicked;
    }

    public bool Checkbox(int id, UiRect rect, string label, ref bool value)
    {
        UiRect box = new(rect.X, rect.Y + Math.Max(0.0f, (rect.Height - 22.0f) * 0.5f), 22.0f, 22.0f);
        bool clicked = Button(id, box, value ? "✓" : string.Empty, true, 1.0f);
        if (clicked)
        {
            value = !value;
        }
        drawList.AddText(label, new Vector2(rect.X + 31.0f, rect.Y + (rect.Height - 16.0f) * 0.5f), UiPalette.Text);
        return clicked;
    }

    public bool SliderFloat(int id, UiRect rect, ref float value, float min, float max)
    {
        bool hovered = rect.Contains(mousePosition);
        bool changed = false;

        if (hovered && (activeId == 0 || activeId == id))
        {
            hotId = id;
            if (activeId == 0 && mousePressed)
            {
                activeId = id;
            }
        }

        bool dragging = activeId == id && (mouseDown || mousePressed);
        if (dragging && max > min)
        {
            float t = Math.Clamp((mousePosition.X - rect.X) / Math.Max(1.0f, rect.Width), 0.0f, 1.0f);
            float next = min + t * (max - min);
            changed = Math.Abs(next - value) > 0.0001f;
            value = next;
        }

        if (activeId == id && mouseReleased)
        {
            activeId = 0;
        }

        float ratio = max > min ? Math.Clamp((value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
        drawList.AddRectFilled(rect, Color.FromBytes(20, 22, 25, 235), rect.Height * 0.5f);
        drawList.AddRectFilled(new UiRect(rect.X, rect.Y, rect.Width * ratio, rect.Height),
                               UiPalette.Accent, rect.Height * 0.5f);
        drawList.AddCircleFilled(new Vector2(rect.X + rect.Width * ratio, rect.Y + rect.Height * 0.5f),
                                 rect.Height * 0.65f, (hovered || dragging) ? Color.White : UiPalette.Text);
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
        drawList.AddRect(rect, UiPalette.Border, rounding, 1.0f);
        if (!string.IsNullOrEmpty(label))
        {
            DrawTextCentered(label, rect, UiPalette.Text, 0.9f);
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

    public void DrawTextCenteredX(string text, float y, Color color, float scale = 1.0f, bool shadow = false)
    {
        Vector2 size = UI.CalcTextSize(text, scale);
        DrawText(text, (ScreenSize.X - size.X) * 0.5f, y, color, scale, shadow);
    }

    public void DrawText(string text, float x, float y, Color color, float scale = 1.0f, bool shadow = false)
    {
        if (shadow)
        {
            drawList.AddText(text, new Vector2(x + 1.0f, y + 1.0f), UiPalette.Shadow, scale);
        }
        drawList.AddText(text, new Vector2(x, y), color, scale);
    }

    public void DrawTextBottomRight(string text, float margin, Color color, float scale = 1.0f, bool shadow = true)
    {
        Vector2 size = UI.CalcTextSize(text, scale);
        DrawText(text, ScreenSize.X - size.X - margin, ScreenSize.Y - size.Y - margin, color, scale, shadow);
    }

    public UiRect PanelAnchored(UiAnchor anchor, Vector2 size, Vector2 offset = default,
                                float rounding = 7.0f, bool captureMouse = false)
    {
        UiRect rect = UiRect.FromAnchor(ScreenSize, anchor, size, offset);
        Panel(rect, rounding, captureMouse);
        return rect;
    }

    public UiRect PanelCenteredX(float width, float y, float height, float rounding = 7.0f)
    {
        UiRect rect = UiRect.FromAnchor(ScreenSize, UiAnchor.TopCenter, new Vector2(width, height), new Vector2(0.0f, y));
        Panel(rect, rounding);
        return rect;
    }

    public UiRect ModalCardCentered(float width, float height, float rounding = 7.0f, byte dimAlpha = 155)
    {
        frameCapturedMouse = true;
        if (dimAlpha > 0)
        {
            drawList.AddRectFilled(new UiRect(0.0f, 0.0f, ScreenSize.X, ScreenSize.Y),
                                   Color.FromBytes(4, 6, 9, dimAlpha), 0.0f, UiDrawLayer.Background);
        }
        UiRect rect = UiRect.FromAnchor(ScreenSize, UiAnchor.Center, new Vector2(width, height));
        Panel(rect, rounding, captureMouse: true);
        return rect;
    }

    public void DrawCrosshair(Vector2 center, float gap = 5.0f, float length = 8.0f,
                              float thickness = 1.5f, Color? color = null)
    {
        Color stroke = color ?? UiPalette.Text;
        drawList.AddLine(new Vector2(center.X - gap - length, center.Y), new Vector2(center.X - gap, center.Y), stroke, thickness);
        drawList.AddLine(new Vector2(center.X + gap, center.Y), new Vector2(center.X + gap + length, center.Y), stroke, thickness);
        drawList.AddLine(new Vector2(center.X, center.Y - gap - length), new Vector2(center.X, center.Y - gap), stroke, thickness);
        drawList.AddLine(new Vector2(center.X, center.Y + gap), new Vector2(center.X, center.Y + gap + length), stroke, thickness);
        drawList.AddCircleFilled(center, 1.5f, stroke);
    }

    public float DrawKeyHintPill(Vector2 pos, string keyLabel, string actionText, float scale = 1.0f)
    {
        Vector2 keySize = UI.CalcTextSize(keyLabel, scale);
        float padX = 7.0f * scale;
        float padY = 3.0f * scale;
        UiRect pillRect = new(pos.X, pos.Y, keySize.X + padX * 2.0f, keySize.Y + padY * 2.0f);
        drawList.AddRectFilled(pillRect, UiPalette.SurfaceElevated, 4.0f);
        drawList.AddRect(pillRect, UiPalette.Border, 4.0f, 1.0f);
        drawList.AddText(keyLabel, new Vector2(pos.X + padX, pos.Y + padY), UiPalette.Highlight, scale);

        if (string.IsNullOrEmpty(actionText))
        {
            return pillRect.Width;
        }

        float gap = 6.0f * scale;
        Vector2 actionSize = UI.CalcTextSize(actionText, scale);
        DrawText(actionText, pillRect.Right + gap, pos.Y + padY, UiPalette.Muted, scale, shadow: true);
        return pillRect.Width + gap + actionSize.X;
    }
}

/// <summary>Path-keyed texture cache; invalid asynchronous requests are retried on later frames.</summary>
public sealed class UiTextureCache
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
