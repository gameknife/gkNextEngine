using GkNext;

namespace ScadPlay;

/// <summary>One node of the loaded level, with the SCAD call that produced it.</summary>
public sealed class LevelNode
{
    public uint Id;
    public uint ParentId;
    /// <summary>The user module that made this node (sp_door), or a loader-made name.</summary>
    public string Name = string.Empty;
    public string Tag = string.Empty;
    public NodeParams Params = new();
    public LevelNode? Parent;
    public readonly List<LevelNode> Children = [];

    /// <summary>Position among the scene's nodes of the same name, in scene order.</summary>
    public int Ordinal;

    /// <summary>1-based line of the top-level statement this node came from, or 0.</summary>
    public int SourceLine;

    /// <summary>
    /// Identity that survives a rebuild: the id when the call has one, otherwise the module name
    /// and its ordinal. Appending to the file keeps every earlier ordinal, which is why agents
    /// append rather than insert.
    /// </summary>
    public string Key => Params.Str("id") is { Length: > 0 } id ? id : $"{Name}#{Ordinal}";

    public LevelNode Root
    {
        get
        {
            LevelNode node = this;
            while (node.Parent != null)
            {
                node = node.Parent;
            }
            return node;
        }
    }

    public IEnumerable<LevelNode> Descendants()
    {
        foreach (LevelNode child in Children)
        {
            yield return child;
            foreach (LevelNode grandChild in child.Descendants())
            {
                yield return grandChild;
            }
        }
    }

    /// <summary>The first direct child whose module name starts with <paramref name="prefix"/>.</summary>
    public LevelNode? Child(string prefix)
    {
        foreach (LevelNode child in Children)
        {
            if (child.Name.StartsWith(prefix, StringComparison.Ordinal))
            {
                return child;
            }
        }
        return null;
    }
}

/// <summary>
/// The level's node tree, read back from the engine after each load.
/// </summary>
/// <remarks>
/// A SCAD level is not built by this game: the loader turns every user-module call into a node and
/// writes the call's parameters into it. This is how the game finds out what the file asked for.
/// Built once per load from Scene.DescribeNodes; never per frame.
/// </remarks>
public sealed class LevelIndex
{
    private const string PlacementTagPrefix = "scad-placement:";

    public readonly List<LevelNode> Nodes = [];
    public readonly Dictionary<uint, LevelNode> ById = [];

    public static LevelIndex Build(string mainSourcePath)
    {
        LevelIndex index = Parse(Scene.DescribeNodes(string.Empty));
        index.ResolveSourceLines(mainSourcePath);
        return index;
    }

    public static LevelIndex Parse(string description)
    {
        LevelIndex index = new();
        Dictionary<string, int> ordinals = new(StringComparer.Ordinal);
        foreach (string line in description.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            string[] fields = line.Split('\t');
            if (fields.Length < 5 || !uint.TryParse(fields[0], out uint id) || !uint.TryParse(fields[1], out uint parent))
            {
                continue;
            }
            LevelNode node = new()
            {
                Id = id,
                ParentId = parent,
                Name = fields[2],
                Tag = fields[3],
                Params = NodeParams.Parse(fields[4]),
            };
            ordinals.TryGetValue(node.Name, out int ordinal);
            node.Ordinal = ordinal;
            ordinals[node.Name] = ordinal + 1;
            index.Nodes.Add(node);
            index.ById[id] = node;
        }

        foreach (LevelNode node in index.Nodes)
        {
            if (index.ById.TryGetValue(node.ParentId, out LevelNode? parent))
            {
                node.Parent = parent;
                parent.Children.Add(node);
            }
        }
        return index;
    }

    public IEnumerable<LevelNode> Roots()
    {
        foreach (LevelNode node in Nodes)
        {
            if (node.Parent == null)
            {
                yield return node;
            }
        }
    }

    /// <summary>
    /// Maps every node to the line of the top-level statement it came from, using the byte offset
    /// the loader tags root statements with. Lets the agent context say "line 42" instead of making
    /// the agent search the file.
    /// </summary>
    private void ResolveSourceLines(string mainSourcePath)
    {
        byte[] bytes;
        try
        {
            bytes = File.ReadAllBytes(mainSourcePath);
        }
        catch (IOException)
        {
            return;
        }
        catch (UnauthorizedAccessException)
        {
            return;
        }

        foreach (LevelNode root in Roots())
        {
            if (!root.Tag.StartsWith(PlacementTagPrefix, StringComparison.Ordinal) ||
                !int.TryParse(root.Tag.AsSpan(PlacementTagPrefix.Length), out int offset) ||
                offset < 0 || offset > bytes.Length)
            {
                continue;
            }
            int line = 1;
            for (int i = 0; i < offset; i++)
            {
                if (bytes[i] == (byte)'\n')
                {
                    line++;
                }
            }
            root.SourceLine = line;
            foreach (LevelNode descendant in root.Descendants())
            {
                descendant.SourceLine = line;
            }
        }
    }
}
