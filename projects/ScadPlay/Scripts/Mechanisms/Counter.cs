namespace ScadPlay;

/// <summary>
/// sp_counter: counts the pieces matched by <c>from</c> -- a module name ("sp_coin") or an id prefix
/// ("coin_") -- that are active, and is active once <c>count</c> of them are (0 = all of them).
/// </summary>
public sealed class Counter : Mechanism
{
    private int have;
    private int need;

    public override void OnTick(float deltaSeconds)
    {
        string from = Params.Str("from", "sp_coin");
        int total = 0;
        have = 0;
        foreach (Mechanism mechanism in World.Mechanisms)
        {
            if (ReferenceEquals(mechanism, this) ||
                (mechanism.Module != from && (mechanism.Id.Length == 0 || !mechanism.Id.StartsWith(from, StringComparison.Ordinal))))
            {
                continue;
            }
            total++;
            if (mechanism.Active)
            {
                have++;
            }
        }
        int count = (int)Params.Num("count", 0.0f);
        need = count > 0 ? count : total;
        bool next = need > 0 && have >= need;
        if (next != Active)
        {
            Active = next;
            World.Log(Active ? $"计数器 {Key} 达成（{have}/{need}）" : $"计数器 {Key} 未达成（{have}/{need}）");
        }
    }

    public override string Describe() => $"{have}/{need}";

    public override void Save(StateBag state) { }

    public override void Load(StateBag state) { }
}
