using GkNext;

namespace ScadPlay;

/// <summary>
/// sp_door: the slab moves by <c>travel</c> (the door's own SCAD frame; default sinks it into the
/// floor) while the inputs are satisfied, and back when they are not -- unless <c>latch</c>.
/// Unwired, it opens for a player within <c>near</c> metres, or stays shut. Active = open.
/// </summary>
public sealed class Door : Mechanism
{
    private MovingPart? slab;
    private Vec3 travel;
    private float open;

    public override void OnStart()
    {
        slab = Part();
        travel = Params.Vector("travel", Vec3.Zero);
        if (travel.LengthSquared() < 1e-6f)
        {
            travel = new Vec3(0.0f, 0.0f, -(Params.Num("h", 3.0f) + 0.2f));
        }
    }

    public override void OnTick(float deltaSeconds)
    {
        float near = Params.Num("near", 0.0f);
        bool wantOpen = HasInputs ? InputsSatisfied() : near > 0.0f && PlayerDistanceXZ(Position) < near;
        if (Params.Flag("latch", false) && Active)
        {
            wantOpen = true;
        }
        if (wantOpen != Active)
        {
            Active = wantOpen;
            World.Log(Active ? $"门 {Key} 打开" : $"门 {Key} 关上");
        }

        float speed = Params.Num("speed", 2.5f) / MathF.Max(travel.Length(), 0.01f);
        open = Mathx.MoveTowards(open, Active ? 1.0f : 0.0f, speed * deltaSeconds);
        slab?.SetPose(travel * open, deltaSeconds);
    }

    public override void Save(StateBag state)
    {
        base.Save(state);
        state.Set("open", open);
    }

    public override void Load(StateBag state)
    {
        base.Load(state);
        open = state.Get("open", Active ? 1.0f : 0.0f);
    }

    public override string Describe() => Active ? $"open {open:P0}" : (open > 0.0f ? "closing" : "closed");
}
