namespace ScadPlay;

/// <summary>
/// sp_portal: stepping into the ring while enabled travels to level <c>to</c> (a file next to this
/// level, without .scad), arriving at its spawn <c>spawn</c>. An empty <c>to</c> means this level.
/// </summary>
public sealed class Portal : Mechanism
{
    private MovingPart? ring;
    private float spin;

    public override void OnStart() => ring = Part();

    public override void OnTick(float deltaSeconds)
    {
        Active = InputsSatisfied();
        if (Active)
        {
            spin += 120.0f * deltaSeconds;
            ring?.SetPose(Vec3.Zero, Vec3.UnitZ, spin, deltaSeconds);
        }
        if (Active && !World.IsLeaving && World.State == PlayState.Playing && PlayerDistanceXZ(Position) < 1.0f &&
            PlayerFeet.Y > Position.Y - 0.3f && PlayerFeet.Y < Position.Y + 2.5f)
        {
            World.Travel(Params.Str("to"), Params.Str("spawn", "start"));
        }
    }

    public override void Save(StateBag state) { }

    public override void Load(StateBag state) { }
}
