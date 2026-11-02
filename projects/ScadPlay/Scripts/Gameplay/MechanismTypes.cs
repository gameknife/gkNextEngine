namespace ScadPlay;

/// <summary>
/// Which C# class drives which kit module. One line per module.
/// </summary>
/// <remarks>
/// The convention is the whole design: a SCAD module call (sp_door) becomes an instance of the
/// class registered for its name (<see cref="Door"/>), bound to that call's node and parameters.
/// To add a kind of gameplay, write the module in assets/scad/lib/kit_scadplay.scad (or any kit
/// the level uses), write a Mechanism subclass for it, and add it here -- then republish this
/// project and the running game picks it up (hot reload).
///
/// An explicit table rather than attribute scanning: reflection over types and
/// Activator.CreateInstance do not survive NativeAOT, and this assembly must build under both
/// backends.
/// </remarks>
public static class MechanismTypes
{
    private static readonly Dictionary<string, Func<Mechanism>> Factories = new(StringComparer.Ordinal)
    {
        ["sp_spawn"] = () => new Spawn(),
        ["sp_level"] = () => new LevelInfo(),
        ["sp_checkpoint"] = () => new Checkpoint(),
        ["sp_coin"] = () => new Coin(),
        ["sp_goal"] = () => new Goal(),
        ["sp_plate"] = () => new Plate(),
        ["sp_lever"] = () => new Lever(),
        ["sp_door"] = () => new Door(),
        ["sp_mover"] = () => new Mover(),
        ["sp_spinner"] = () => new Spinner(),
        ["sp_bridge"] = () => new Bridge(),
        ["sp_jumppad"] = () => new JumpPad(),
        ["sp_hazard"] = () => new Hazard(),
        ["sp_sweeper"] = () => new Sweeper(),
        ["sp_enemy"] = () => new Enemy(),
        ["sp_sign"] = () => new Sign(),
        ["sp_portal"] = () => new Portal(),
        ["sp_counter"] = () => new Counter(),
        ["sp_logic"] = () => new Logic(),
        ["sp_crumble"] = () => new Crumble(),
    };

    public static IEnumerable<string> Modules => Factories.Keys;

    public static Mechanism? Create(string module) =>
        Factories.TryGetValue(module, out Func<Mechanism>? factory) ? factory() : null;
}
