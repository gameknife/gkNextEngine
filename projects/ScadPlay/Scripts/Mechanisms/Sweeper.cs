namespace ScadPlay;

/// <summary>sp_sweeper: a bar turning about a post at <c>speed</c> °/s. Touching the bar kills.</summary>
public sealed class Sweeper : Mechanism
{
    private const float BarRadius = 0.18f;

    private MovingPart? arm;
    private float angle;

    public override void OnStart() => arm = Part();

    public override void OnTick(float deltaSeconds)
    {
        Active = InputsSatisfied();
        if (Active)
        {
            angle = (angle + Params.Num("speed", 90.0f) * deltaSeconds) % 360.0f;
        }
        arm?.SetPose(Vec3.Zero, Vec3.UnitZ, angle, deltaSeconds);

        if (World.State != PlayState.Playing)
        {
            return;
        }
        float length = Params.Num("len", 4.0f);
        float height = Params.Num("height", 0.6f);
        float radians = angle * MathF.PI / 180.0f;
        Vec3 tip = Position + LocalToWorldDirection(new Vec3(MathF.Cos(radians), MathF.Sin(radians), 0.0f)) * length;

        Vec3 feet = PlayerFeet;
        float barY = Position.Y + height;
        bool overlapsHeight = feet.Y < barY + BarRadius + 0.1f && feet.Y + Player.CapsuleHeight > barY - BarRadius;
        if (overlapsHeight && DistanceToSegmentXZ(feet, Position, tip) < BarRadius + Player.CapsuleRadius)
        {
            World.Kill("被扫杆打中");
        }
    }

    private static float DistanceToSegmentXZ(Vec3 point, Vec3 a, Vec3 b)
    {
        Vec3 ab = ScadSpace.Flat(b - a);
        Vec3 ap = ScadSpace.Flat(point - a);
        float t = Math.Clamp(Vec3.Dot(ap, ab) / MathF.Max(ab.LengthSquared(), 1e-6f), 0.0f, 1.0f);
        return (ap - ab * t).Length();
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
