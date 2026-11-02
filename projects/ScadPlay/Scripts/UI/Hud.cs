using GkNext;
using GkNext.Interop;

namespace ScadPlay;

/// <summary>
/// The HUD: progress on the left, live-editing status on the right (build result, warnings,
/// script reloads), prompts and signs at the bottom, and with L every piece's id, state and
/// wiring drawn over the world.
/// </summary>
/// <remarks>
/// The right-hand column is for the person talking to the agent: it is how they see that the edit
/// they asked for has landed, or why it has not, without reading a log.
/// </remarks>
public sealed class Hud
{
    private const float Margin = 16.0f;
    private const float RightColumnWidth = 380.0f;

    private readonly ManagedImGui gui = new();
    private readonly Dictionary<(string, int), List<string>> wrapCache = [];

    public bool ShowLabels { get; set; }

    public void Draw(ScadPlayGame game)
    {
        gui.BeginFrame();
        World? world = game.World;
        if (world != null)
        {
            if (ShowLabels)
            {
                DrawLabels(game, world);
            }
            DrawProgress(world);
            DrawToasts(world);
            DrawBottom(world);
            DrawStateOverlay(world);
        }
        DrawLiveStatus(game);
        DrawHints(game);
        gui.EndFrame();
    }

    private void DrawProgress(World world)
    {
        string title = world.LevelTitle.Length > 0 ? world.LevelTitle : "ScadPlay";
        bool hasGoal = world.LevelGoal.Length > 0;
        UiRect panel = new(Margin, Margin, 360.0f, hasGoal ? 92.0f : 66.0f);
        gui.Panel(panel, 9.0f);
        float y = panel.Y + 10.0f;
        gui.DrawText(title, panel.X + 14.0f, y, HudPalette.Text, 1.3f);
        y += 26.0f;
        if (hasGoal)
        {
            gui.DrawText(world.LevelGoal, panel.X + 14.0f, y, HudPalette.Muted);
            y += 24.0f;
        }
        string stats = world.CoinsTotal > 0
            ? $"金币 {world.Coins}/{world.CoinsTotal}    死亡 {world.Deaths}    {world.LevelTime:F1} 秒"
            : $"死亡 {world.Deaths}    {world.LevelTime:F1} 秒";
        gui.DrawText(stats, panel.X + 14.0f, y, world.State == PlayState.Won ? HudPalette.Success : HudPalette.Highlight);
    }

    private void DrawLiveStatus(ScadPlayGame game)
    {
        LiveLevel live = game.Live;
        float x = gui.ScreenSize.X - RightColumnWidth - Margin;
        float y = Margin;

        string mode = live.IsLive ? "实时" : "只读";
        Color dot = live.Building ? HudPalette.Highlight : live.Ok ? HudPalette.Success : HudPalette.Danger;
        string state = live.Building ? "构建中…" : live.Ok ? $"成功 {live.Milliseconds / 1000.0:F2} 秒" : "失败";
        UiRect header = new(x, y, RightColumnWidth, 34.0f);
        gui.Panel(header, 8.0f);
        gui.DrawList.AddCircleFilled(new Vector2(header.X + 16.0f, header.Y + 17.0f), 5.0f, dot);
        gui.DrawText($"{live.LevelName}.scad · {mode} · 构建 #{live.Serial} {state}", header.X + 28.0f, header.Y + 8.0f,
                     HudPalette.Text, 0.95f);
        y = header.Bottom + 6.0f;

        if (!live.Ok && live.Error.Length > 0)
        {
            y = DrawNote(x, y, live.HasScene ? "构建失败，保留上一版关卡：" : "构建失败：", live.Error, HudPalette.Danger, 6);
        }
        if (live.WarningCount > 0)
        {
            string first = live.Warnings.Count > 0 ? live.Warnings[0] : string.Empty;
            y = DrawNote(x, y, $"{live.WarningCount} 条 SCAD 警告：", first, HudPalette.Warning, 3);
        }
        World? world = game.World;
        if (world != null)
        {
            foreach (Mechanism mechanism in world.Mechanisms)
            {
                if (mechanism.Fault != null)
                {
                    y = DrawNote(x, y, $"{Mechanism.Describe(mechanism.Node)} 出错：", mechanism.Fault, HudPalette.Danger, 3);
                    break;
                }
            }
            if (world.Unbound.Count > 0)
            {
                y = DrawNote(x, y, "没有 C# 类的玩法件：", string.Join("、", world.Unbound), HudPalette.Warning, 2);
            }
        }
        if (game.ScriptSerial > 0)
        {
            gui.DrawText($"C# 第 {game.ScriptSerial} 次热重载 · {game.ScriptLoadedAt.ToLocalTime():HH:mm:ss}", x + 4.0f, y,
                         HudPalette.Muted, 0.9f, shadow: true);
        }
    }

    private float DrawNote(float x, float y, string heading, string text, Color color, int maxLines)
    {
        List<string> lines = Wrap(text, RightColumnWidth - 28.0f, 0.9f);
        int shown = Math.Min(lines.Count, maxLines);
        float height = 32.0f + shown * 18.0f;
        UiRect rect = new(x, y, RightColumnWidth, height);
        gui.Panel(rect, 8.0f);
        gui.DrawList.AddRectFilled(new UiRect(rect.X, rect.Y, 4.0f, rect.Height), color, 2.0f);
        gui.DrawText(heading, rect.X + 14.0f, rect.Y + 8.0f, color, 0.95f);
        for (int i = 0; i < shown; i++)
        {
            string line = i == maxLines - 1 && lines.Count > maxLines ? lines[i] + " …" : lines[i];
            gui.DrawText(line, rect.X + 14.0f, rect.Y + 28.0f + i * 18.0f, HudPalette.Text, 0.9f);
        }
        return rect.Bottom + 6.0f;
    }

    private void DrawToasts(World world)
    {
        float y = gui.ScreenSize.Y * 0.26f;
        foreach ((string text, ToastKind kind, float remaining) in world.Toasts)
        {
            Color color = kind switch
            {
                ToastKind.Good => HudPalette.Success,
                ToastKind.Bad => HudPalette.Danger,
                _ => HudPalette.Text,
            };
            float alpha = Math.Clamp(remaining * 2.0f, 0.0f, 1.0f);
            gui.DrawTextCenteredX(text, y, new Color(color.R, color.G, color.B, alpha), 1.7f, shadow: true);
            y += 40.0f;
        }
    }

    private void DrawBottom(World world)
    {
        float y = gui.ScreenSize.Y - 110.0f;
        if (world.SignText.Length > 0)
        {
            List<string> lines = Wrap(world.SignText, 680.0f, 1.05f);
            float height = 20.0f + lines.Count * 22.0f;
            UiRect panel = gui.PanelCenteredX(720.0f, y - height, height, 9.0f);
            for (int i = 0; i < lines.Count; i++)
            {
                gui.DrawText(lines[i], panel.X + 20.0f, panel.Y + 10.0f + i * 22.0f, HudPalette.Text, 1.05f);
            }
            y = panel.Y - 10.0f;
        }
        if (world.Prompt.Length > 0)
        {
            int space = world.Prompt.IndexOf(' ');
            string key = space > 0 ? world.Prompt[..space] : world.Prompt;
            string action = space > 0 ? world.Prompt[(space + 1)..] : string.Empty;
            Vector2 keySize = UI.CalcTextSize(key, 1.2f);
            Vector2 actionSize = UI.CalcTextSize(action, 1.2f);
            float width = keySize.X + actionSize.X + 32.0f;
            gui.DrawKeyHintPill(new Vector2((gui.ScreenSize.X - width) * 0.5f, y - 34.0f), key, action, 1.2f);
        }
    }

    private void DrawStateOverlay(World world)
    {
        if (world.State == PlayState.Dead)
        {
            gui.DrawList.AddRectFilled(new UiRect(0.0f, 0.0f, gui.ScreenSize.X, gui.ScreenSize.Y),
                                       Color.FromBytes(120, 10, 10, 70), 0.0f, UiDrawLayer.Background);
        }
        else if (world.State == PlayState.Won)
        {
            UiRect panel = gui.PanelCenteredX(420.0f, gui.ScreenSize.Y * 0.38f, 96.0f, 12.0f);
            gui.DrawTextCentered("通关！", new UiRect(panel.X, panel.Y + 10.0f, panel.Width, 40.0f), HudPalette.Success, 1.8f);
            gui.DrawTextCentered($"{world.LevelTime:F1} 秒 · 金币 {world.Coins}/{world.CoinsTotal} · 按 R 重开",
                                 new UiRect(panel.X, panel.Y + 54.0f, panel.Width, 30.0f), HudPalette.Muted);
        }
    }

    private void DrawHints(ScadPlayGame game)
    {
        float x = Margin;
        float y = gui.ScreenSize.Y - 34.0f;
        x += gui.DrawKeyHintPill(new Vector2(x, y), "WASD", "移动") + 14.0f;
        x += gui.DrawKeyHintPill(new Vector2(x, y), "空格", "跳") + 14.0f;
        x += gui.DrawKeyHintPill(new Vector2(x, y), "Shift", "冲刺") + 14.0f;
        x += gui.DrawKeyHintPill(new Vector2(x, y), game.Camera.PointerLocked ? "鼠标" : "右键拖动", "视角") + 14.0f;
        x += gui.DrawKeyHintPill(new Vector2(x, y), "E", "互动") + 14.0f;
        x += gui.DrawKeyHintPill(new Vector2(x, y), "R", "重开") + 14.0f;
        x += gui.DrawKeyHintPill(new Vector2(x, y), "L", ShowLabels ? "隐藏标注" : "显示标注") + 14.0f;
        gui.DrawKeyHintPill(new Vector2(x, y), "Tab", game.Camera.PointerLocked ? "释放鼠标" : "锁定鼠标");
    }

    /// <summary>
    /// Every piece's id, module and state at its place in the world, with a line from each piece it
    /// listens to. The in-game picture of what the level file wires together.
    /// </summary>
    private void DrawLabels(ScadPlayGame game, World world)
    {
        Vector2 screen = gui.ScreenSize;
        Dictionary<Mechanism, Vector2> anchors = [];
        foreach (Mechanism mechanism in world.Mechanisms)
        {
            // Only what can be wired: pieces with an id, or pieces listening to one.
            if (mechanism is Spawn or Sign or LevelInfo || mechanism is Coin { Active: true } ||
                (mechanism.Id.Length == 0 && !mechanism.HasInputs))
            {
                continue;
            }
            Vec3 at = mechanism.WorldBounds.IsValid
                ? new Vec3(mechanism.Position.X, mechanism.WorldBounds.Max.Y + 0.4f, mechanism.Position.Z)
                : mechanism.Position + new Vec3(0.0f, 1.5f, 0.0f);
            if (Vec3.Distance(at, game.Camera.Position) > 45.0f || !game.Camera.Project(at, screen, out Vector2 pixel))
            {
                continue;
            }
            anchors[mechanism] = pixel;
        }

        foreach ((Mechanism mechanism, Vector2 pixel) in anchors)
        {
            foreach (string id in mechanism.On.Concat(mechanism.Any))
            {
                if (world.Find(id) is Mechanism source && anchors.TryGetValue(source, out Vector2 from))
                {
                    Color wire = source.Active ? HudPalette.Success : HudPalette.Highlight;
                    gui.DrawList.AddLine(from, pixel, new Color(wire.R, wire.G, wire.B, 0.8f), 2.0f);
                }
            }
        }

        foreach ((Mechanism mechanism, Vector2 pixel) in anchors)
        {
            string name = mechanism.Id.Length > 0 ? mechanism.Id : $"{mechanism.Module}#{mechanism.Node.Ordinal}";
            string text = $"{name} · {(mechanism.Fault != null ? "出错" : mechanism.Describe())}";
            Vector2 size = UI.CalcTextSize(text, 0.85f);
            UiRect box = new(pixel.X - size.X * 0.5f - 6.0f, pixel.Y - size.Y - 6.0f, size.X + 12.0f, size.Y + 6.0f);
            Color edge = mechanism.Fault != null ? HudPalette.Danger : mechanism.Active ? HudPalette.Success : HudPalette.Border;
            gui.DrawList.AddRectFilled(box, Color.FromBytes(20, 22, 26, 200), 4.0f);
            gui.DrawList.AddRect(box, edge, 4.0f, 1.5f);
            gui.DrawText(text, box.X + 6.0f, box.Y + 3.0f, HudPalette.Text, 0.85f);
        }
    }

    /// <summary>
    /// Greedy wrap by measured width. Character by character, so CJK text (no spaces) wraps too;
    /// each glyph is measured once and the widths summed, which ignores kerning but keeps a long
    /// error message from costing thousands of measurements.
    /// </summary>
    private List<string> Wrap(string text, float width, float scale)
    {
        (string, int) key = (text, (int)(width * scale));
        if (wrapCache.TryGetValue(key, out List<string>? cached))
        {
            return cached;
        }
        List<string> lines = [];
        foreach (string paragraph in text.Split('\n'))
        {
            int start = 0;
            float lineWidth = 0.0f;
            int lastSpace = -1;
            for (int i = 0; i < paragraph.Length; i++)
            {
                float glyph = GlyphWidth(paragraph[i], scale);
                if (lineWidth + glyph > width && i > start)
                {
                    int cut = lastSpace > start ? lastSpace : i;
                    lines.Add(paragraph[start..cut].Trim());
                    start = cut;
                    lastSpace = -1;
                    lineWidth = 0.0f;
                    for (int j = start; j <= i; j++)
                    {
                        lineWidth += GlyphWidth(paragraph[j], scale);
                    }
                    continue;
                }
                if (paragraph[i] == ' ')
                {
                    lastSpace = i + 1;
                }
                lineWidth += glyph;
            }
            if (start < paragraph.Length || paragraph.Length == 0)
            {
                lines.Add(paragraph[start..].Trim());
            }
        }
        if (wrapCache.Count > 64)
        {
            wrapCache.Clear();
        }
        wrapCache[key] = lines;
        return lines;
    }

    private readonly Dictionary<(char, float), float> glyphWidths = [];

    private float GlyphWidth(char c, float scale)
    {
        if (!glyphWidths.TryGetValue((c, scale), out float width))
        {
            width = UI.CalcTextSize(c.ToString(), scale).X;
            glyphWidths[(c, scale)] = width;
        }
        return width;
    }
}
