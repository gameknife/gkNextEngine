namespace ScadPlay;

/// <summary>sp_plate: active while the player stands on it; latch = stays active once stepped on.</summary>
public sealed class Plate : Mechanism
{
    private MovingPart? cap;

    public override void OnStart() => cap = Part();

    public override void OnTick(float deltaSeconds)
    {
        float half = Params.Num("w", 1.4f) * 0.5f + 0.15f;
        bool pressed = World.State == PlayState.Playing && PlayerStandsOn(Position, half, 0.45f);
        bool next = Params.Flag("latch", false) ? Active || pressed : pressed;
        if (next != Active)
        {
            Active = next;
            World.Log(Active ? $"踏板 {Key} 被踩下" : $"踏板 {Key} 弹起");
        }
        cap?.SetPose(new Vec3(0.0f, 0.0f, Active ? -0.06f : 0.0f), deltaSeconds);
    }
}
