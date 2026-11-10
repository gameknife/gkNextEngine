using GkNext;
using GkNext.Interop;

namespace ScadPlay;

/// <summary>sp_coin: picked up on touch (active = taken). Adds <c>value</c> to the coin count.</summary>
public sealed class Coin : Mechanism
{
    private const float PickupRadius = 0.9f;
    private const float SpinRadiansPerSecond = 2.5f;

    private Rot bindRotation;
    private Vec3 center;
    private float spin;

    public int Value => (int)Params.Num("value", 1.0f);

    public override void OnStart()
    {
        bindRotation = new NodeRef(Node.Id).Rotation.N();
        center = WorldBounds.IsValid ? WorldBounds.Center.N() : Position + new Vec3(0.0f, Params.Num("hover", 0.9f), 0.0f);
        World.CoinsTotal += Value;
    }

    public override void OnTick(float deltaSeconds)
    {
        if (Active)
        {
            return;
        }
        spin += SpinRadiansPerSecond * deltaSeconds;
        Scene.SetNodeRotation(Node.Id, Rot.Normalize(bindRotation * Rot.CreateFromAxisAngle(Vec3.UnitY, spin)).Gk());

        Vec3 body = PlayerFeet + new Vec3(0.0f, Player.CapsuleHeight * 0.5f, 0.0f);
        if (World.State == PlayState.Playing && ScadSpace.Flat(body - center).Length() < PickupRadius &&
            MathF.Abs(body.Y - center.Y) < 1.1f)
        {
            Take();
            World.Log($"吃到金币 {Key}（{World.Coins}/{World.CoinsTotal}）");
        }
    }

    private void Take()
    {
        Active = true;
        World.Coins += Value;
        Scene.SetNodeVisibleRecursive(Node.Id, false);
    }

    public override void Load(StateBag state)
    {
        if (state.Get("active", false) && !Active)
        {
            Take();
        }
    }
}
