namespace ScadPlay;

/// <summary>sp_hazard: lava, spikes or water. Touching it kills the player (back to the checkpoint).</summary>
public sealed class Hazard : Mechanism
{
    public override void OnStart() => Active = true;

    public override void OnTick(float deltaSeconds)
    {
        // A little forgiving at the rim: the capsule has to be over the surface, not merely brushing it.
        if (World.State == PlayState.Playing && PlayerTouches(WorldBounds, -0.2f))
        {
            World.Kill(Params.Str("kind", "lava") switch
            {
                "spikes" => "踩到了尖刺",
                "water" => "掉进了水里",
                _ => "掉进了岩浆",
            });
        }
    }

    public override void Save(StateBag state) { }

    public override void Load(StateBag state) { }
}
