namespace ScadPlay;

/// <summary>sp_sign: shows its <c>text</c> at the bottom of the screen while the player is within <c>r</c> metres.</summary>
public sealed class Sign : Mechanism
{
    public override void OnTick(float deltaSeconds)
    {
        Active = PlayerDistanceXZ(Position) < Params.Num("r", 3.0f);
        if (Active)
        {
            World.SignText = Params.Str("text");
        }
    }

    public override void Save(StateBag state) { }

    public override void Load(StateBag state) { }
}
