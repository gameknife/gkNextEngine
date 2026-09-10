using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// Yaw and pitch, turned into a camera basis.
/// </summary>
/// <remarks>
/// Look reads <see cref="Input.GetMouseDelta"/>, not cursor position. Pointer lock
/// (<see cref="Input.SetRelativeMouseMode"/>) is what makes that usable at the screen edge and
/// inside the editor viewport: without it the cursor would stop at the window border and F5 Play
/// would be a drag-to-look toy.
///
/// When the pointer is not locked — ejected Play, or a host that refused capture — holding the
/// right mouse button still looks, so the same code is playable without a grabbed cursor.
///
/// Angles are stored, not a matrix. Everything else is derived, which keeps the one piece of state
/// that can drift down to two floats.
/// </remarks>
internal sealed class LookController(float sensitivity)
{
    /// <summary>SDL button numbering, which is what the input bindings carry: 1 left, 2 middle,
    /// 3 right.</summary>
    private const int RightMouseButton = 3;

    /// <summary>Just under a right angle: at exactly 90 degrees the forward vector is parallel to
    /// world up and the camera basis collapses.</summary>
    private const float PitchLimit = 1.55f;

    private bool ignoreNextDelta;

    public float Yaw { get; private set; }
    public float Pitch { get; private set; } = -0.15f;

    /// <summary>Where the camera is pointing.</summary>
    public Vector3 Forward
    {
        get
        {
            float cosPitch = MathF.Cos(Pitch);
            return new Vector3(MathF.Sin(Yaw) * cosPitch, MathF.Sin(Pitch), -MathF.Cos(Yaw) * cosPitch);
        }
    }

    /// <summary>Camera right, flat on the ground plane — strafing should not climb.</summary>
    public Vector3 Right => new(MathF.Cos(Yaw), 0.0f, MathF.Sin(Yaw));

    /// <summary>Forward with the pitch removed, so walking forward while looking down still walks
    /// forward at the same speed.</summary>
    public Vector3 FlatForward => new(MathF.Sin(Yaw), 0.0f, -MathF.Cos(Yaw));

    public void Reset(float yaw, float pitch)
    {
        Yaw = yaw;
        Pitch = pitch;
        ignoreNextDelta = true;
    }

    /// <summary>Drops the next motion sample. Pointer lock warps the cursor; without this the
    /// view snaps by however far the pointer was from the window centre.</summary>
    public void SuppressNextDelta() => ignoreNextDelta = true;

    /// <summary>Call once per frame, before the camera is read.</summary>
    public void Update()
    {
        Vector2 delta = Input.GetMouseDelta();
        bool looking = Input.IsRelativeMouseMode() || Input.IsMouseButtonDown(RightMouseButton);
        if (ignoreNextDelta)
        {
            ignoreNextDelta = false;
            return;
        }

        if (!looking)
        {
            return;
        }

        Yaw += delta.X * sensitivity;
        Pitch = Mathx.Clamp(Pitch - delta.Y * sensitivity, -PitchLimit, PitchLimit);
    }
}
