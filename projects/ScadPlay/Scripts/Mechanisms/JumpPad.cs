namespace ScadPlay;

/// <summary>sp_jumppad: stepping on it while enabled throws the player <c>launch</c> metres up.</summary>
public sealed class JumpPad : Mechanism
{
    private MovingPart? cap;
    private float squash;
    private float cooldown;

    public override void OnStart() => cap = Part();

    public override void OnTick(float deltaSeconds)
    {
        Active = InputsSatisfied();
        cooldown = MathF.Max(0.0f, cooldown - deltaSeconds);
        float radius = Params.Num("r", 0.9f);
        if (Active && cooldown <= 0.0f && World.State == PlayState.Playing &&
            PlayerStandsOn(Position + new Vec3(0.0f, 0.32f, 0.0f), radius + 0.1f, 0.4f))
        {
            World.Player.Launch(Params.Num("launch", 6.0f));
            squash = 1.0f;
            cooldown = 0.4f;
        }
        squash = MathF.Max(0.0f, squash - 4.0f * deltaSeconds);
        cap?.SetPose(new Vec3(0.0f, 0.0f, -0.1f * squash), deltaSeconds);
    }
}
