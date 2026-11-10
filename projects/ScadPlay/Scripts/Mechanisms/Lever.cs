using GkNext;

namespace ScadPlay;

/// <summary>sp_lever: walk up and press E. toggle = flips each time; otherwise it can be pulled once.</summary>
public sealed class Lever : Mechanism
{
    private const float Reach = 2.0f;
    private const float Swing = 70.0f;

    private MovingPart? arm;
    private float angle;

    public override void OnStart() => arm = Part();

    public override void OnTick(float deltaSeconds)
    {
        bool toggle = Params.Flag("toggle", true);
        float distance = PlayerDistanceXZ(Position);
        if (World.State == PlayState.Playing && distance < Reach && (toggle || !Active) &&
            World.OfferInteraction(this, Active ? "E 扳回拉杆" : "E 扳动拉杆", distance))
        {
            Active = !Active;
            World.Log(Active ? $"拉杆 {Key} 扳下" : $"拉杆 {Key} 扳回");
        }
        angle = Mathx.MoveTowards(angle, Active ? Swing : 0.0f, 240.0f * deltaSeconds);
        arm?.SetPose(Vec3.Zero, Vec3.UnitY, angle, deltaSeconds);
    }

    public override void Load(StateBag state)
    {
        base.Load(state);
        angle = Active ? Swing : 0.0f;
    }
}
