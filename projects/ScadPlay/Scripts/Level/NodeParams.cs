using System.Globalization;

namespace ScadPlay;

/// <summary>
/// The named parameters of one SCAD module call, as the loader wrote them into the node's
/// metadata: "k=v;k=v", with a flat vector spelled as its elements joined by ','.
/// </summary>
/// <remarks>
/// Every getter takes the fallback the kit module declares, so a level that leaves a parameter at
/// its default and a level that omits it read the same. Numbers must parse whole: a vector read as
/// a number falls back instead of silently becoming its first element.
/// </remarks>
public sealed class NodeParams
{
    private static readonly string[] NoIds = [];
    private readonly Dictionary<string, string> values = new(StringComparer.Ordinal);

    public static NodeParams Parse(string metadata)
    {
        NodeParams result = new();
        foreach (string record in metadata.Split(';', StringSplitOptions.RemoveEmptyEntries))
        {
            int equals = record.IndexOf('=');
            if (equals <= 0)
            {
                continue;
            }
            result.values[record[..equals]] = record[(equals + 1)..];
        }
        return result;
    }

    public IReadOnlyDictionary<string, string> All => values;

    public bool Has(string key) => values.ContainsKey(key);

    public string Str(string key, string fallback = "") =>
        values.TryGetValue(key, out string? value) ? value : fallback;

    public float Num(string key, float fallback)
    {
        if (values.TryGetValue(key, out string? value) &&
            float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out float parsed) &&
            float.IsFinite(parsed))
        {
            return parsed;
        }
        return fallback;
    }

    public bool Flag(string key, bool fallback)
    {
        if (!values.TryGetValue(key, out string? value))
        {
            return fallback;
        }
        return value switch
        {
            "true" or "1" => true,
            "false" or "0" => false,
            _ => fallback,
        };
    }

    /// <summary>A SCAD-space vector parameter (two elements leave z at 0).</summary>
    public Vec3 Vector(string key, Vec3 fallback)
    {
        if (!values.TryGetValue(key, out string? value))
        {
            return fallback;
        }
        string[] parts = value.Split(',');
        if (parts.Length < 2 || parts.Length > 3)
        {
            return fallback;
        }
        Span<float> numbers = stackalloc float[3];
        numbers.Clear();
        for (int i = 0; i < parts.Length; i++)
        {
            if (!float.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out numbers[i]))
            {
                return fallback;
            }
        }
        return new Vec3(numbers[0], numbers[1], numbers[2]);
    }

    /// <summary>
    /// A list of ids: on = "a" and on = ["a", "b"] both arrive here, the second as "a,b".
    /// </summary>
    public string[] Ids(string key)
    {
        if (!values.TryGetValue(key, out string? value) || value.Length == 0)
        {
            return NoIds;
        }
        return value.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
    }
}
