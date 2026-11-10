namespace ScadPlay;

/// <summary>sp_spinner: turns about its own vertical axis at <c>speed</c> °/s while enabled.</summary>
public sealed class Spinner : Mechanism
{
    private MovingPart? top;
    private float angle;

    public override void OnStart() => top = Part();

    public override void OnTick(float deltaSeconds)
    {
        Active = InputsSatisfied();
        if (Active)
        {
            angle = (angle + Params.Num("speed", 40.0f) * deltaSeconds) % 360.0f;
        }
        top?.SetPose(Vec3.Zero, Vec3.UnitZ, angle, deltaSeconds);
    }

    public override void Save(StateBag state)
    {
        base.Save(state);
        state.Set("angle", angle);
    }

    public override void Load(StateBag state)
    {
        base.Load(state);
        angle = state.Get("angle", angle);
    }
}
