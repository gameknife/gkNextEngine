using GkNext;

namespace ScadPlay;

/// <summary>
/// sp_bridge: a drawbridge built standing up; it swings down flat while its inputs are satisfied
/// and back up when they are not. Active = fully down.
/// </summary>
public sealed class Bridge : Mechanism
{
    private MovingPart? deck;
    private float lowered;
    private bool reported;

    public override void OnStart() => deck = Part();

    public override void OnTick(float deltaSeconds)
    {
        bool down = InputsSatisfied();
        float rate = Params.Num("speed", 60.0f) / 90.0f;
        lowered = Mathx.MoveTowards(lowered, down ? 1.0f : 0.0f, rate * deltaSeconds);
        // The kit stands the deck up with rotate([0, -90, 0]); turning +90° about the same axis lays it flat.
        deck?.SetPose(Vec3.Zero, Vec3.UnitY, 90.0f * Smooth(lowered), deltaSeconds);

        Active = lowered >= 0.999f;
        if (Active != reported)
        {
            reported = Active;
            World.Log(Active ? $"吊桥 {Key} 放下" : $"吊桥 {Key} 升起");
        }
    }

    private static float Smooth(float x) => x * x * (3.0f - 2.0f * x);

    public override void Save(StateBag state)
    {
        base.Save(state);
        state.Set("lowered", lowered);
    }

    public override void Load(StateBag state)
    {
        base.Load(state);
        lowered = state.Get("lowered", lowered);
        reported = Active;
    }
}
