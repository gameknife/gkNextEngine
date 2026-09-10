using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// The camera that orbits the player, and the basis every movement decision is made in.
/// </summary>
/// <remarks>
/// Look reads <see cref="Input.GetMouseDelta"/> under pointer lock, so the view turns at the
/// screen edge and inside the editor viewport. Holding the right button is still "aim": the
/// camera closes in over the shoulder. When the pointer is not locked, the same button both
/// aims and looks, which is what F8 eject and a host that refused capture fall back to.
/// </remarks>
internal sealed class TpsCamera(float sensitivity)
{
    /// SDL button numbering, which is what the input bindings carry: 1 left, 2 middle, 3 right.
    private const int RightMouseButton = 3;

    private const float PitchMin = -0.55f;
    private const float PitchMax = 0.85f;
    private const float HipDistance = 6.2f;
    private const float AimDistance = 3.6f;
    private const float HipShoulder = 0.0f;
    private const float AimShoulder = 0.85f;
    private const float FocusHeight = 1.5f;

    private bool ignoreNextDelta;
    private float distance = HipDistance;
    private float shoulder = HipShoulder;

    public float Yaw { get; private set; }
    public float Pitch { get; private set; } = 0.25f;

    /// <summary>True while the right button is held: the player aims, the camera closes in.</summary>
    public bool Aiming { get; private set; }

    /// <summary>Camera forward flattened onto the ground plane — what "forward" means to the
    /// player, which is not the same as what it means to the camera when it is looking down.</summary>
    public Vector3 FlatForward => new(MathF.Sin(Yaw), 0.0f, -MathF.Cos(Yaw));

    public Vector3 FlatRight => new(MathF.Cos(Yaw), 0.0f, MathF.Sin(Yaw));

    /// <summary>Where the camera is actually pointing, pitch included. Shots travel along this.</summary>
    public Vector3 Forward
    {
        get
        {
            float cosPitch = MathF.Cos(Pitch);
            return new Vector3(MathF.Sin(Yaw) * cosPitch, -MathF.Sin(Pitch), -MathF.Cos(Yaw) * cosPitch);
        }
    }

    public void Reset(float yaw)
    {
        Yaw = yaw;
        Pitch = 0.25f;
        ignoreNextDelta = true;
        Aiming = false;
        distance = HipDistance;
        shoulder = HipShoulder;
    }

    /// <summary>Drops the next motion sample. Pointer lock warps the cursor; without this the
    /// view snaps by however far the pointer was from the window centre.</summary>
    public void SuppressNextDelta() => ignoreNextDelta = true;

    /// <summary>Call once per frame, before anything reads the basis.</summary>
    public void Update(float deltaSeconds)
    {
        Vector2 delta = Input.GetMouseDelta();
        Aiming = Input.IsMouseButtonDown(RightMouseButton);
        bool looking = Input.IsRelativeMouseMode() || Aiming;

        if (ignoreNextDelta)
        {
            ignoreNextDelta = false;
        }
        else if (looking)
        {
            Yaw += delta.X * sensitivity;
            Pitch = Mathx.Clamp(Pitch + delta.Y * sensitivity, PitchMin, PitchMax);
        }

        // Eased rather than snapped: the camera moving in is the feedback that says "you are
        // aiming now", and a cut reads as a glitch.
        float targetDistance = Aiming ? AimDistance : HipDistance;
        float targetShoulder = Aiming ? AimShoulder : HipShoulder;
        // Frame-rate independent easing: a plain lerp by a constant factor converges faster on a
        // fast machine, so the same camera feels different at 60fps and 144fps.
        float blend = 1.0f - MathF.Exp(-10.0f * deltaSeconds);
        distance = Mathx.Lerp(distance, targetDistance, blend);
        shoulder = Mathx.Lerp(shoulder, targetShoulder, blend);
    }

    /// <summary>Fills the engine's camera for a player standing at <paramref name="focus"/>.</summary>
    public void Apply(ref CameraOverride camera, Vector3 focus)
    {
        Vector3 forward = Forward;
        Vector3 right = FlatRight;

        float focusX = focus.X + right.X * shoulder;
        float focusY = focus.Y + FocusHeight;
        float focusZ = focus.Z + right.Z * shoulder;

        camera.Position = new Vector3(focusX - forward.X * distance,
                                      focusY - forward.Y * distance,
                                      focusZ - forward.Z * distance);
        camera.Target = new Vector3(focusX, focusY, focusZ);
        camera.Up = Vector3.Up;
        camera.FieldOfView = Aiming ? 50.0f : 60.0f;
    }
}
