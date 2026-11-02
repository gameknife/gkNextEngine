using GkNext;

namespace ScadPlay;

/// <summary>sp_checkpoint: touching it makes it the respawn point (active from then on).</summary>
public sealed class Checkpoint : Mechanism
{
    private const float FlagRise = 1.6f;

    private MovingPart? flag;
    private float raised;

    public float FacingYaw => ScadSpace.CharacterYaw(LocalToWorldDirection(Vec3.UnitX));

    public override void OnStart() => flag = Part();

    public override void OnTick(float deltaSeconds)
    {
        float radius = Params.Num("r", 1.2f);
        if (!Active && World.State == PlayState.Playing && PlayerDistanceXZ(Position) <= radius &&
            MathF.Abs(PlayerFeet.Y - Position.Y) < 2.0f)
        {
            Active = true;
            World.SetRespawn(Position, World.Player.Yaw, Key);
            World.Log($"到达检查点 {Key}");
            World.Toast("检查点", ToastKind.Good);
        }
        raised = Mathx.MoveTowards(raised, Active ? 1.0f : 0.0f, 1.5f * deltaSeconds);
        flag?.SetPose(new Vec3(0.0f, 0.0f, FlagRise * raised), deltaSeconds);
    }

    public override void Load(StateBag state)
    {
        base.Load(state);
        raised = Active ? 1.0f : 0.0f;
    }
}
