namespace ScadPlay;

/// <summary>
/// sp_logic: active when its inputs are satisfied (on = all, any = one of), negated by
/// <c>invert</c>, and only after the condition has held for <c>delay</c> seconds.
/// </summary>
public sealed class Logic : Mechanism
{
    private float held;

    public override void OnTick(float deltaSeconds)
    {
        bool condition = InputsSatisfied(false) ^ Params.Flag("invert", false);
        if (condition == Active)
        {
            held = 0.0f;
            return;
        }
        held += deltaSeconds;
        if (held >= Params.Num("delay", 0.0f))
        {
            Active = condition;
            held = 0.0f;
            World.Log(Active ? $"逻辑 {Key} 成立" : $"逻辑 {Key} 不成立");
        }
    }
}
