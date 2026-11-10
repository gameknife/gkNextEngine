namespace ScadPlay;

/// <summary>sp_spawn: where the player starts (id "start" unless a portal names another).</summary>
public sealed class Spawn : Mechanism
{
    /// <summary>The spawn's +X in SCAD, as a character yaw.</summary>
    public float FacingYaw => ScadSpace.CharacterYaw(LocalToWorldDirection(Vec3.UnitX));

    public override void OnStart() => Active = true;

    public override void Save(StateBag state) { }

    public override void Load(StateBag state) { }
}
