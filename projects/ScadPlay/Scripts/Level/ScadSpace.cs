global using Vec3 = System.Numerics.Vector3;
global using Rot = System.Numerics.Quaternion;

using GkNext.Interop;

namespace ScadPlay;

/// <summary>
/// Conversions between the three spaces this game deals in.
/// </summary>
/// <remarks>
/// Gameplay math uses System.Numerics (<see cref="Vec3"/>, <see cref="Rot"/>) because the binding
/// structs are deliberately bare: they carry data across the ABI and have no operators. Only the
/// call sites that touch the engine convert.
///
/// SCAD is Z-up, the engine is Y-up, one unit is one metre in both, and the loader maps
/// SCAD (x, y, z) to engine (x, z, -y). That map is a proper rotation, so it also carries
/// directions and rotation axes, and the agent context speaks SCAD so that its numbers drop
/// straight into a translate([x, y, z]).
/// </remarks>
public static class ScadSpace
{
    public static Vec3 N(this Vector3 v) => new(v.X, v.Y, v.Z);

    public static Vector3 Gk(this Vec3 v) => new(v.X, v.Y, v.Z);

    public static Rot N(this Vector4 q) => new(q.X, q.Y, q.Z, q.W);

    public static Vector4 Gk(this Rot q) => new(q.X, q.Y, q.Z, q.W);

    /// <summary>SCAD point or direction to engine space.</summary>
    public static Vec3 ToEngine(Vec3 scad) => new(scad.X, scad.Z, -scad.Y);

    /// <summary>Engine point or direction to SCAD space.</summary>
    public static Vec3 ToScad(Vec3 engine) => new(engine.X, -engine.Z, engine.Y);

    /// <summary>
    /// The heading of an engine-space direction as SCAD's yaw: degrees counter-clockwise from +X
    /// seen from above, i.e. what rotate([0, 0, yaw]) needs to turn an object's +X that way.
    /// </summary>
    public static float ScadYawDegrees(Vec3 engineDirection)
    {
        Vec3 scad = ToScad(engineDirection);
        return MathF.Atan2(scad.Y, scad.X) * (180.0f / MathF.PI);
    }

    /// <summary>The flat engine direction a SCAD yaw points along.</summary>
    public static Vec3 EngineDirectionFromScadYaw(float degrees)
    {
        float radians = degrees * (MathF.PI / 180.0f);
        return ToEngine(new Vec3(MathF.Cos(radians), MathF.Sin(radians), 0.0f));
    }

    /// <summary>
    /// Character yaw as the rig and the controller use it: radians, 0 facing engine +Z, measured
    /// so that (sin yaw, 0, cos yaw) is the facing direction.
    /// </summary>
    public static float CharacterYaw(Vec3 engineDirection) => MathF.Atan2(engineDirection.X, engineDirection.Z);

    public static Vec3 CharacterForward(float yaw) => new(MathF.Sin(yaw), 0.0f, MathF.Cos(yaw));

    public static Vec3 Flat(Vec3 v) => new(v.X, 0.0f, v.Z);

    public static Vec3 SafeNormalize(Vec3 v, Vec3 fallback)
    {
        float length = v.Length();
        return length > 1e-5f ? v / length : fallback;
    }
}
