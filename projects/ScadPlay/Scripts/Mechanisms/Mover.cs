using GkNext;

namespace ScadPlay;

/// <summary>
/// sp_mover: a platform between its start and start + <c>travel</c>. mode "loop" goes back and
/// forth (pausing <c>wait</c> seconds at each end) while enabled; mode "toggle" goes to the far end
/// while enabled and back when not. Active = enabled.
/// </summary>
public sealed class Mover : Mechanism
{
    private MovingPart? deck;
    private Vec3 travel;
    private float t;
    private float direction = 1.0f;
    private float waitTimer;

    public override void OnStart()
    {
        deck = Part();
        travel = Params.Vector("travel", new Vec3(0.0f, 0.0f, 3.0f));
    }

    public override void OnTick(float deltaSeconds)
    {
        Active = InputsSatisfied();
        float rate = Params.Num("speed", 1.5f) / MathF.Max(travel.Length(), 0.01f);

        if (Params.Str("mode", "loop") == "toggle")
        {
            t = Mathx.MoveTowards(t, Active ? 1.0f : 0.0f, rate * deltaSeconds);
        }
        else if (Active)
        {
            if (waitTimer > 0.0f)
            {
                waitTimer -= deltaSeconds;
            }
            else
            {
                t += direction * rate * deltaSeconds;
                if (t >= 1.0f || t <= 0.0f)
                {
                    t = Mathx.Saturate(t);
                    direction = -direction;
                    waitTimer = Params.Num("wait", 1.0f);
                }
            }
        }
        deck?.SetPose(travel * Smooth(t), deltaSeconds);
    }

    /// <summary>Eased ends: a deck that stops dead throws the rider.</summary>
    private static float Smooth(float x) => x * x * (3.0f - 2.0f * x);

    public override void Save(StateBag state)
    {
        base.Save(state);
        state.Set("t", t);
        state.Set("direction", direction);
    }

    public override void Load(StateBag state)
    {
        base.Load(state);
        t = state.Get("t", t);
        direction = state.Get("direction", direction);
    }

    public override string Describe() => $"{(Active ? "running" : "stopped")} at {t:P0}";
}
