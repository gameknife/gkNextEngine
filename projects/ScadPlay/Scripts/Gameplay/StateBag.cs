using System.Globalization;
using System.Text.Json;

namespace ScadPlay;

/// <summary>
/// A mechanism's saved state: a few named numbers and strings.
/// </summary>
/// <remarks>
/// Deliberately untyped. It outlives the code that wrote it -- a hot reload brings a new version
/// of the class that reads it -- so a field the new code no longer has is ignored and a field it
/// gained reads its default, instead of the whole bag failing to deserialize.
/// </remarks>
public sealed class StateBag
{
    private readonly Dictionary<string, double> numbers = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> strings = new(StringComparer.Ordinal);

    public bool IsEmpty => numbers.Count == 0 && strings.Count == 0;

    public void Set(string key, float value) => numbers[key] = value;

    public void Set(string key, bool value) => numbers[key] = value ? 1.0 : 0.0;

    public void Set(string key, string value) => strings[key] = value;

    public float Get(string key, float fallback) =>
        numbers.TryGetValue(key, out double value) ? (float)value : fallback;

    public bool Get(string key, bool fallback) =>
        numbers.TryGetValue(key, out double value) ? value != 0.0 : fallback;

    public string Get(string key, string fallback) =>
        strings.TryGetValue(key, out string? value) ? value : fallback;

    public void Write(Utf8JsonWriter writer)
    {
        writer.WriteStartObject();
        foreach ((string key, double value) in numbers)
        {
            writer.WriteNumber(key, value);
        }
        foreach ((string key, string value) in strings)
        {
            writer.WriteString(key, value);
        }
        writer.WriteEndObject();
    }

    public static StateBag Read(JsonElement element)
    {
        StateBag bag = new();
        if (element.ValueKind != JsonValueKind.Object)
        {
            return bag;
        }
        foreach (JsonProperty property in element.EnumerateObject())
        {
            switch (property.Value.ValueKind)
            {
                case JsonValueKind.Number:
                    bag.numbers[property.Name] = property.Value.GetDouble();
                    break;
                case JsonValueKind.String:
                    bag.strings[property.Name] = property.Value.GetString() ?? string.Empty;
                    break;
            }
        }
        return bag;
    }

    public override string ToString()
    {
        List<string> parts = [];
        foreach ((string key, double value) in numbers)
        {
            parts.Add($"{key}={value.ToString("0.###", CultureInfo.InvariantCulture)}");
        }
        foreach ((string key, string value) in strings)
        {
            parts.Add($"{key}={value}");
        }
        return string.Join(' ', parts);
    }
}
