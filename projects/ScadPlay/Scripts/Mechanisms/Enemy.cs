namespace ScadPlay;

/// <summary>
/// sp_enemy: walks between its start and start + <c>travel</c>. Touching it kills the player;
/// landing on it from above (if <c>stompable</c>) defeats it. Active = defeated.
/// </summary>
public sealed class Enemy : Mechanism
{
    private const float BodyRadius = 0.55f;
    private const float BodyHeight = 0.9f;

    private MovingPart? body;
    private Vec3 travel;
    private float t;
    private float direction = 1.0f;

    public override void OnStart()
    {
        body = Part();
        travel = Params.Vector("travel", new Vec3(4.0f, 0.0f, 0.0f));
    }

    public override void OnTick(float deltaSeconds)
    {
        if (Active)
        {
            return;
        }
        float rate = Params.Num("speed", 1.6f) / MathF.Max(travel.Length(), 0.01f);
        t += direction * rate * deltaSeconds;
        if (t >= 1.0f || t <= 0.0f)
        {
            t = Math.Clamp(t, 0.0f, 1.0f);
            direction = -direction;
        }
        // Face the way it walks: the body's eyes are on its +X.
        float facing = MathF.Atan2(travel.Y * direction, travel.X * direction) * 180.0f / MathF.PI;
        body?.SetPose(travel * t, Vec3.UnitZ, facing, deltaSeconds);

        if (World.State != PlayState.Playing)
        {
            return;
        }
        Vec3 center = Position + LocalToWorldDirection(travel * t);
        Vec3 feet = PlayerFeet;
        bool near = ScadSpace.Flat(feet - center).Length() < BodyRadius + Player.CapsuleRadius;
        bool overlaps = feet.Y < center.Y + BodyHeight && feet.Y + Player.CapsuleHeight > center.Y;
        if (!near || !overlaps)
        {
            return;
        }
        if (Params.Flag("stompable", true) && World.Player.Velocity.Y < -0.5f && feet.Y > center.Y + BodyHeight * 0.5f)
        {
            Defeat();
            World.Player.Bounce(7.0f);
            World.Log($"踩扁了敌人 {Key}");
            World.Toast("踩扁！", ToastKind.Good, 1.2f);
        }
        else
        {
            World.Kill("被敌人碰到了");
        }
    }

    private void Defeat()
    {
        Active = true;
        body?.SetVisible(false);
    }

    public override void Save(StateBag state)
    {
        base.Save(state);
        state.Set("t", t);
        state.Set("direction", direction);
    }

    public override void Load(StateBag state)
    {
        t = state.Get("t", t);
        direction = state.Get("direction", direction);
        if (state.Get("active", false))
        {
            Defeat();
        }
    }

    public override string Describe() => Active ? "defeated" : "patrolling";
}
