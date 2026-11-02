using GkNext;
using GkNext.Interop;

namespace ScadPlay;

/// <summary>
/// Third-person camera: orbits the player, eases in behind them while they run, and pulls in
/// when the level gets between it and the player.
/// </summary>
/// <remarks>
/// Look is "hold the right mouse button and drag" by default rather than pointer lock, on purpose:
/// this game is played next to a chat with an agent, and a captured cursor would have to be
/// released every time the player wants to type. Tab toggles pointer lock for those who want it.
/// </remarks>
public sealed class FollowCamera
{
    private const int RightMouseButton = 3;
    private const float Sensitivity = 0.0045f;
    private const float PitchMin = -0.35f;
    private const float PitchMax = 1.2f;
    private const float Distance = 7.0f;
    private const float FocusHeight = 1.4f;
    private const float BoomRadius = 0.3f;
    private const float FollowRate = 2.0f;

    private float boom = Distance;

    /// <summary>Heading in radians; the camera looks along (sin yaw, 0, -cos yaw).</summary>
    public float Yaw { get; set; }

    public float Pitch { get; set; } = 0.38f;

    public bool PointerLocked { get; private set; }

    public Vec3 Position { get; private set; }
    public Vec3 Target { get; private set; }
    public float FieldOfView { get; } = 60.0f;

    public Vec3 FlatForward => new(MathF.Sin(Yaw), 0.0f, -MathF.Cos(Yaw));

    public Vec3 FlatRight => new(MathF.Cos(Yaw), 0.0f, MathF.Sin(Yaw));

    public Vec3 Forward
    {
        get
        {
            float cosPitch = MathF.Cos(Pitch);
            return new Vec3(MathF.Sin(Yaw) * cosPitch, -MathF.Sin(Pitch), -MathF.Cos(Yaw) * cosPitch);
        }
    }

    /// <summary>Faces the camera the way a character with <paramref name="characterYaw"/> faces.</summary>
    public void SnapBehind(float characterYaw)
    {
        Vec3 facing = ScadSpace.CharacterForward(characterYaw);
        Yaw = MathF.Atan2(facing.X, -facing.Z);
    }

    public void TogglePointerLock()
    {
        PointerLocked = !PointerLocked;
        Input.SetRelativeMouseMode(PointerLocked);
    }

    public void Release()
    {
        PointerLocked = false;
        Input.SetRelativeMouseMode(false);
    }

    public void Update(float deltaSeconds, Vec3 playerVelocity, bool playerMoving)
    {
        Vector2 delta = Input.GetMouseDelta();
        bool dragging = Input.IsMouseButtonDown(RightMouseButton);
        if (PointerLocked || dragging)
        {
            Yaw += delta.X * Sensitivity;
            Pitch = Mathx.Clamp(Pitch + delta.Y * Sensitivity, PitchMin, PitchMax);
        }

        // Without a hand on the view, drift in behind a player running away from the camera. Only
        // then: following a strafe swings "right" round with the camera and the player runs in a
        // circle, and following a retreat turns the camera to face the way the player came from.
        if (!PointerLocked && !dragging && playerMoving)
        {
            Vec3 flat = ScadSpace.Flat(playerVelocity);
            float speed = flat.Length();
            if (speed > 1.0f && Vec3.Dot(flat / speed, FlatForward) > 0.7f)
            {
                float behind = MathF.Atan2(flat.X, -flat.Z);
                Yaw = Mathx.TurnTowards(Yaw, behind, FollowRate * deltaSeconds * MathF.Min(1.0f, speed / 6.0f));
            }
        }
    }

    /// <summary>Places the camera for a player whose feet are at <paramref name="feet"/>.</summary>
    public void Apply(ref CameraOverride camera, Vec3 feet, float deltaSeconds)
    {
        Vec3 focus = feet + new Vec3(0.0f, FocusHeight, 0.0f);
        Vec3 back = -Forward;

        // The boom collapses at once when something is in the way (seeing through a wall for a
        // frame is worse than a cut) and eases back out.
        float wanted = Distance;
        RayHit hit = Scene.RayCast(focus.Gk(), back.Gk(), Distance + BoomRadius);
        if (hit.IsHit)
        {
            wanted = MathF.Max(hit.Distance - BoomRadius, 0.6f);
        }
        boom = wanted < boom ? wanted : Mathx.MoveTowards(boom, wanted, 6.0f * deltaSeconds);

        Position = focus + back * boom;
        Target = focus;
        camera.Position = Position.Gk();
        camera.Target = Target.Gk();
        camera.Up = Vector3.Up;
        camera.FieldOfView = FieldOfView;
    }

    /// <summary>
    /// Projects a world point to UI pixels, or returns false behind the camera. Used for the debug
    /// labels that show each piece's id and wiring.
    /// </summary>
    public bool Project(Vec3 world, Vector2 screen, out Vector2 pixel)
    {
        pixel = default;
        Vec3 forward = Vec3.Normalize(Target - Position);
        Vec3 right = Vec3.Normalize(Vec3.Cross(forward, Vec3.UnitY));
        Vec3 up = Vec3.Cross(right, forward);
        Vec3 d = world - Position;
        float z = Vec3.Dot(d, forward);
        if (z < 0.2f)
        {
            return false;
        }
        float tanHalf = MathF.Tan(FieldOfView * 0.5f * MathF.PI / 180.0f);
        float aspect = screen.X / MathF.Max(screen.Y, 1.0f);
        float ndcX = Vec3.Dot(d, right) / (z * tanHalf * aspect);
        float ndcY = Vec3.Dot(d, up) / (z * tanHalf);
        if (MathF.Abs(ndcX) > 1.3f || MathF.Abs(ndcY) > 1.3f)
        {
            return false;
        }
        pixel = new Vector2((ndcX + 1.0f) * 0.5f * screen.X, (1.0f - ndcY) * 0.5f * screen.Y);
        return true;
    }
}
