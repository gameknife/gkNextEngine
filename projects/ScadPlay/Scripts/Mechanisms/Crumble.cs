namespace ScadPlay;

/// <summary>
/// sp_crumble: a slab that gives way <c>warn</c> seconds after the player lands on it and comes
/// back <c>respawn</c> seconds later. Active = fallen.
/// </summary>
public sealed class Crumble : Mechanism
{
    private MovingPart? slab;
    private float timer;
    private bool armed;

    public override void OnStart() => slab = Part();

    public override void OnTick(float deltaSeconds)
    {
        if (slab == null)
        {
            return;
        }
        if (Active)
        {
            timer -= deltaSeconds;
            if (timer <= 0.0f)
            {
                Active = false;
                slab.SetVisible(true);
                slab.SetPose(Vec3.Zero, deltaSeconds);
            }
            return;
        }

        if (!armed && World.Player.IsOnGround && slab.CarriesBody(World.Player.GroundBodyId))
        {
            armed = true;
            timer = Params.Num("warn", 0.6f);
        }
        if (!armed)
        {
            slab.SetPose(Vec3.Zero, deltaSeconds);
            return;
        }

        timer -= deltaSeconds;
        float shake = 0.04f * MathF.Sin(timer * 70.0f);
        slab.SetPose(new Vec3(shake, 0.0f, 0.0f), deltaSeconds);
        if (timer <= 0.0f)
        {
            armed = false;
            Active = true;
            timer = Params.Num("respawn", 3.0f);
            slab.SetVisible(false);
            World.Log($"易碎平台 {Key} 塌了");
        }
    }

    public override void Save(StateBag state) { }

    public override void Load(StateBag state) { }
}
