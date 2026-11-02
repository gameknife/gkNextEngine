using GkNext;

namespace ScadPlay;

/// <summary>
/// sp_goal: touching it finishes the level (active = reached). Closed until its inputs are
/// satisfied; <c>next</c> names the level to go to afterwards.
/// </summary>
public sealed class Goal : Mechanism
{
    private MovingPart? ring;
    private float spin;
    private bool wasOpen;

    public override void OnStart() => ring = Part();

    public override void OnTick(float deltaSeconds)
    {
        bool open = InputsSatisfied();
        if (open)
        {
            spin += 90.0f * deltaSeconds;
            ring?.SetPose(Vec3.Zero, Vec3.UnitZ, spin, deltaSeconds);
        }
        if (open != wasOpen)
        {
            wasOpen = open;
            World.Log(open ? $"终点 {Key} 开放" : $"终点 {Key} 关闭");
            if (open)
            {
                World.Toast("终点已开放", ToastKind.Good);
            }
        }

        bool touching = PlayerDistanceXZ(Position) < 1.3f && PlayerFeet.Y > Position.Y - 0.5f &&
                        PlayerFeet.Y < Position.Y + 2.5f;
        if (!touching || Active || World.State != PlayState.Playing)
        {
            return;
        }
        if (!open)
        {
            World.SignText = $"终点还没开放（需要：{string.Join("、", On.Concat(Any))}）";
            return;
        }
        Active = true;
        World.CompleteLevel(Params.Str("next"));
    }

    public override string Describe() => Active ? "reached" : (InputsSatisfied() ? "open" : "locked");
}
