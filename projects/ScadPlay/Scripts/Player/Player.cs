using GkNext;
using GkNext.Interop;

namespace ScadPlay;

/// <summary>
/// The player character: a capsule controller that walks the SCAD level's collision, and the
/// astro_bot rig drawn on top of it.
/// </summary>
/// <remarks>
/// The game integrates the whole velocity (gravity, jump arc, launch pads) and hands it to the
/// controller with ground inheritance on, which is what keeps the player on a moving platform.
/// Tuning follows NextAstrobot, whose levels share this scale: a 2 m jump, a ~5 m running jump.
///
/// The controller and the rig are rebuilt on every level load (the physics world and the rig pools
/// are per scene); <see cref="Position"/>, <see cref="Velocity"/> and <see cref="Yaw"/> are what
/// carry the player across a rebuild.
/// </remarks>
public sealed class Player
{
    public const float CapsuleHeight = 1.5f;
    public const float CapsuleRadius = 0.35f;
    private const float StepHeight = 0.55f;
    private const float RunSpeed = 6.0f;
    private const float SprintSpeed = 8.5f;
    private const float RunAccel = 50.0f;
    private const float AirControl = 0.8f;
    private const float Gravity = 20.0f;
    private const float JumpSpeed = 8.9f;
    private const float JumpCut = 0.5f;
    private const float CoyoteSeconds = 0.12f;
    private const float JumpBufferSeconds = 0.15f;
    private const float TurnRateRadians = 900.0f * MathF.PI / 180.0f;
    private const float MaxFallSpeed = 40.0f;

    private const string RigPath = "assets/scad/characters/astro_bot.scad";

    private uint character;
    private uint rigPool;
    private uint rig;
    private string clip = string.Empty;
    private float coyote;
    private float jumpBuffer;
    private bool jumpRising;
    private float actionClipTimer;

    public Vec3 Position { get; private set; }
    public Vec3 Velocity { get; private set; }
    public float Yaw { get; private set; }
    public bool IsOnGround { get; private set; }
    public uint GroundBodyId { get; private set; } = PhysicsBodyIds.Invalid;
    public bool Exists => character != 0;

    public Vec3 Forward => ScadSpace.CharacterForward(Yaw);

    /// <summary>The rig's root node, or NodeIds.Invalid before the rig exists.</summary>
    public uint RigRootNodeId => rig != 0 ? Rig.GetRootNodeId(rig) : NodeIds.Invalid;

    /// <summary>Rig pools can only be declared while the scene is being built.</summary>
    public void DeclareRig()
    {
        rigPool = Rig.IsAvailable() ? Rig.DeclarePool(RigPath, 1) : 0;
        rig = 0;
        clip = string.Empty;
    }

    /// <summary>Creates the controller and the rig at a foot position, after the level is committed.</summary>
    public void Spawn(Vec3 footPosition, float yaw, Vec3 velocity)
    {
        Despawn();
        CharacterSpec spec = new(footPosition.Gk(), CapsuleHeight, CapsuleRadius)
        {
            MaxStepHeight = StepHeight,
            MaxSlopeDegrees = 50.0f,
        };
        character = Character.Create(in spec);
        if (character == 0)
        {
            Log.Error("[ScadPlay] the physics backend refused the player character");
        }
        Position = footPosition;
        Velocity = velocity;
        Yaw = yaw;
        IsOnGround = false;

        if (rigPool != 0)
        {
            rig = Rig.Acquire(rigPool, footPosition.Gk(), yaw, new Vector3(0.20f, 0.45f, 0.85f));
        }
        PlayClip("idle", 0.0f);
    }

    public void Despawn()
    {
        if (character != 0)
        {
            Character.Destroy(character);
            character = 0;
        }
        // The rig belongs to the scene and goes with it; only forget the handle.
        rig = 0;
        clip = string.Empty;
    }

    public void Teleport(Vec3 footPosition, float yaw)
    {
        Position = footPosition;
        Velocity = Vec3.Zero;
        Yaw = yaw;
        jumpRising = false;
        if (character != 0)
        {
            Character.SetPosition(character, footPosition.Gk());
        }
        PlayClip("idle", 0.1f);
    }

    /// <summary>Throws the player up to <paramref name="height"/> metres above where they stand.</summary>
    public void Launch(float height)
    {
        Velocity = new Vec3(Velocity.X, MathF.Sqrt(2.0f * Gravity * MathF.Max(height, 0.1f)), Velocity.Z);
        IsOnGround = false;
        jumpRising = false;
        coyote = 0.0f;
    }

    /// <summary>A small hop, e.g. after stomping an enemy.</summary>
    public void Bounce(float speed)
    {
        Velocity = new Vec3(Velocity.X, speed, Velocity.Z);
        IsOnGround = false;
        jumpRising = false;
    }

    public void OnDied()
    {
        Velocity = Vec3.Zero;
        PlayClip("hurt", 0.05f);
        actionClipTimer = 1.0f;
    }

    public void OnWon()
    {
        PlayClip("win", 0.1f);
        actionClipTimer = 3.0f;
    }

    /// <summary>
    /// One frame of movement. <paramref name="viewForward"/> / <paramref name="viewRight"/> are the
    /// camera's flat basis: stick input is camera-relative.
    /// </summary>
    public void Tick(float deltaSeconds, bool controls, MoveAxis move, Vec3 viewForward, Vec3 viewRight,
                     bool jumpPressed, bool jumpHeld, bool sprint)
    {
        if (character == 0 || deltaSeconds <= 0.0f)
        {
            return;
        }

        Vec3 wish = Vec3.Zero;
        if (controls && move.IsMoving)
        {
            wish = viewForward * move.Forward + viewRight * move.Right;
            float length = wish.Length();
            if (length > 1.0f)
            {
                wish /= length;
            }
        }

        Vec3 velocity = Velocity;
        float control = IsOnGround ? 1.0f : AirControl;
        Vec3 desired = wish * (sprint ? SprintSpeed : RunSpeed);
        velocity.X = Mathx.MoveTowards(velocity.X, desired.X, RunAccel * control * deltaSeconds);
        velocity.Z = Mathx.MoveTowards(velocity.Z, desired.Z, RunAccel * control * deltaSeconds);

        if (wish.LengthSquared() > 0.0025f)
        {
            Yaw = Mathx.TurnTowards(Yaw, ScadSpace.CharacterYaw(wish), TurnRateRadians * deltaSeconds);
        }

        coyote = IsOnGround ? CoyoteSeconds : MathF.Max(0.0f, coyote - deltaSeconds);
        jumpBuffer = controls && jumpPressed ? JumpBufferSeconds : MathF.Max(0.0f, jumpBuffer - deltaSeconds);
        if (jumpBuffer > 0.0f && coyote > 0.0f)
        {
            velocity.Y = JumpSpeed;
            jumpRising = true;
            jumpBuffer = 0.0f;
            coyote = 0.0f;
            IsOnGround = false;
        }
        else if (IsOnGround && velocity.Y <= 0.0f)
        {
            // A small downward bias keeps the controller's stick-to-floor in contact with slopes
            // and with platforms that are moving down under the player.
            velocity.Y = -1.0f;
            jumpRising = false;
        }
        else
        {
            velocity.Y = MathF.Max(velocity.Y - Gravity * deltaSeconds, -MaxFallSpeed);
        }
        if (jumpRising && !jumpHeld && velocity.Y > 0.0f)
        {
            velocity.Y *= JumpCut;
            jumpRising = false;
        }

        Character.Move(character, velocity.Gk(), true, deltaSeconds);
        CharacterState state = Character.GetState(character);

        Vec3 previous = Position;
        Position = state.FootPosition.N();
        bool wasOnGround = IsOnGround;
        IsOnGround = state.IsOnGround;
        GroundBodyId = IsOnGround ? state.GroundBodyId : PhysicsBodyIds.Invalid;
        // A ceiling eats the requested rise; keep the integrated velocity honest or the jump arc
        // hangs under the ceiling.
        if (velocity.Y > 0.0f && Position.Y - previous.Y < 0.001f)
        {
            velocity.Y = 0.0f;
            jumpRising = false;
        }
        if (IsOnGround && !wasOnGround && velocity.Y < 0.0f)
        {
            velocity.Y = -1.0f;
        }
        Velocity = velocity;

        UpdateRig(deltaSeconds);
    }

    private void UpdateRig(float deltaSeconds)
    {
        if (rig == 0)
        {
            return;
        }
        Rig.SetTransform(rig, Position.Gk(), Yaw);

        actionClipTimer = MathF.Max(0.0f, actionClipTimer - deltaSeconds);
        if (actionClipTimer > 0.0f)
        {
            return;
        }
        float speed = ScadSpace.Flat(Velocity).Length();
        if (IsOnGround)
        {
            PlayClip(speed > 0.4f ? "run" : "idle", 0.12f);
        }
        else
        {
            PlayClip(Velocity.Y > 0.0f ? "jump" : "fall", 0.1f);
        }
    }

    private void PlayClip(string name, float fade)
    {
        if (rig == 0 || name == clip || !Rig.HasClip(rigPool, name))
        {
            return;
        }
        clip = name;
        Rig.PlayClip(rig, name, fade);
    }
}
