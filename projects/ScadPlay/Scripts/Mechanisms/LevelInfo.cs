namespace ScadPlay;

/// <summary>sp_level: the level's title and goal line for the HUD. Draws nothing.</summary>
public sealed class LevelInfo : Mechanism
{
    public override void OnStart()
    {
        World.LevelTitle = Params.Str("title");
        World.LevelGoal = Params.Str("goal");
        Active = true;
    }

    public override void Save(StateBag state) { }

    public override void Load(StateBag state) { }

    public override string Describe() => World.LevelTitle;
}
