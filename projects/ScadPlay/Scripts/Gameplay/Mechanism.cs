using GkNext;
using GkNext.Interop;

namespace ScadPlay;

/// <summary>
/// One gameplay piece of the level: the C# behind one SCAD module call.
/// </summary>
/// <remarks>
/// The class for a module is registered in <see cref="MechanismTypes"/> under the module's name
/// (sp_door -> <c>Door</c>). Everything a piece needs to know about itself comes from the SCAD
/// call: its parameters (<see cref="Params"/>), where it stands (<see cref="Position"/>, already in
/// engine space) and its moving sub-parts (<see cref="Part"/>).
///
/// Wiring is by id: a piece with an <c>id</c> is a signal other pieces can listen to with
/// <c>on</c> (all must be active) and <c>any</c> (one is enough). <see cref="Active"/> is this
/// piece's output; <see cref="InputsSatisfied"/> reads its inputs.
///
/// State that must survive a rebuild of the level (a door that has opened stays open, a coin that
/// has been taken stays taken) goes through <see cref="Save"/> / <see cref="Load"/>; it is carried
/// over by <see cref="LevelNode.Key"/>, across level rebuilds and across C# hot reloads alike.
/// </remarks>
public abstract class Mechanism
{
    private World? world;
    private LevelNode? node;

    protected World World => world ?? throw new InvalidOperationException("mechanism is not bound");

    public LevelNode Node => node ?? throw new InvalidOperationException("mechanism is not bound");

    public NodeParams Params => Node.Params;

    public string Module => Node.Name;

    /// <summary>The SCAD id, or empty for an anonymous piece.</summary>
    public string Id { get; private set; } = string.Empty;

    public string Key => Node.Key;

    /// <summary>This piece's output signal.</summary>
    public bool Active { get; protected set; }

    /// <summary>Set once the piece threw; it is skipped from then on and reported to the agent.</summary>
    public string? Fault { get; internal set; }

    /// <summary>World position of the module call's origin, engine space.</summary>
    public Vec3 Position { get; private set; }

    /// <summary>World rotation of the module call, engine space.</summary>
    public Rot Rotation { get; private set; } = Rot.Identity;

    /// <summary>World-space bounds of everything the call draws (invalid for marker modules).</summary>
    public Bounds WorldBounds { get; private set; }

    public string[] On { get; private set; } = [];
    public string[] Any { get; private set; } = [];

    public bool HasInputs => On.Length > 0 || Any.Length > 0;

    /// <summary>One line for the agent context and the debug labels.</summary>
    public virtual string Describe() => Active ? "active" : "idle";

    internal void Bind(World owner, LevelNode levelNode)
    {
        world = owner;
        node = levelNode;
        Id = levelNode.Params.Str("id");
        On = levelNode.Params.Ids("on");
        Any = levelNode.Params.Ids("any");

        NodeTransform transform = Scene.GetNodeWorldTransform(levelNode.Id);
        Position = transform.Translation.N();
        Rotation = Rot.Normalize(transform.Rotation.N());
        WorldBounds = Scene.GetNodeBounds(levelNode.Id, false);
    }

    /// <summary>Called once after every piece of the level exists, so wiring can be resolved.</summary>
    public virtual void OnStart() { }

    public virtual void OnTick(float deltaSeconds) { }

    public virtual void Save(StateBag state) => state.Set("active", Active);

    public virtual void Load(StateBag state) => Active = state.Get("active", Active);

    // --- inputs -----------------------------------------------------------------------------

    /// <summary>
    /// True when every id in <c>on</c> is active and, if <c>any</c> is given, at least one of those.
    /// With no inputs at all the answer is <paramref name="whenUnwired"/>: a mover with nothing to
    /// listen to just runs, a door with nothing to listen to stays shut.
    /// </summary>
    protected bool InputsSatisfied(bool whenUnwired = true)
    {
        if (!HasInputs)
        {
            return whenUnwired;
        }
        foreach (string id in On)
        {
            if (!World.Signal(id))
            {
                return false;
            }
        }
        if (Any.Length == 0)
        {
            return true;
        }
        foreach (string id in Any)
        {
            if (World.Signal(id))
            {
                return true;
            }
        }
        return false;
    }

    // --- space --------------------------------------------------------------------------------

    /// <summary>A SCAD vector in this piece's own frame (it turns with rotate()) as an engine-space
    /// world direction. travel = [0, 0, 3] on a piece is "3 m along its local up".</summary>
    protected Vec3 LocalToWorldDirection(Vec3 scadLocal) => Vec3.Transform(ScadSpace.ToEngine(scadLocal), Rotation);

    protected Vec3 PlayerFeet => World.Player.Position;

    /// <summary>Horizontal distance from the player to <paramref name="point"/>.</summary>
    protected float PlayerDistanceXZ(Vec3 point)
    {
        Vec3 delta = ScadSpace.Flat(PlayerFeet - point);
        return delta.Length();
    }

    /// <summary>
    /// Is the player standing on a disc of <paramref name="radius"/> around <paramref name="center"/>
    /// whose surface is at <paramref name="center"/>.Y? Used by plates and pads.
    /// </summary>
    protected bool PlayerStandsOn(Vec3 center, float radius, float heightTolerance = 0.35f)
    {
        return World.Player.IsOnGround && PlayerDistanceXZ(center) <= radius &&
               MathF.Abs(PlayerFeet.Y - center.Y) <= heightTolerance;
    }

    /// <summary>
    /// Does the player's capsule overlap <paramref name="bounds"/>? <paramref name="horizontalMargin"/>
    /// grows (or, negative, shrinks) the box sideways -- a little forgiveness at a rim -- and
    /// <paramref name="verticalMargin"/> grows it up and down, so feet resting on a thin slab count.
    /// </summary>
    protected bool PlayerTouches(in Bounds bounds, float horizontalMargin = 0.0f, float verticalMargin = 0.05f)
    {
        if (!bounds.IsValid)
        {
            return false;
        }
        Vec3 feet = PlayerFeet;
        float radius = Player.CapsuleRadius + horizontalMargin;
        return feet.X >= bounds.Min.X - radius && feet.X <= bounds.Max.X + radius &&
               feet.Z >= bounds.Min.Z - radius && feet.Z <= bounds.Max.Z + radius &&
               feet.Y + Player.CapsuleHeight >= bounds.Min.Y - verticalMargin &&
               feet.Y <= bounds.Max.Y + verticalMargin;
    }

    /// <summary>A moving sub-part: the first child module whose name starts with <paramref name="prefix"/>
    /// (sp_part_ by default). Null when the level wrapped it somewhere the contract does not allow.</summary>
    protected MovingPart? Part(string prefix = "sp_part_")
    {
        LevelNode? partNode = Node.Child(prefix);
        if (partNode == null)
        {
            World.Warn($"{Describe(Node)} has no {prefix}* child; it cannot move (keep part calls out of gk_flatten)");
            return null;
        }
        return new MovingPart(partNode, body => World.RegisterBody(body, this));
    }

    internal static string Describe(LevelNode node) =>
        node.Params.Str("id") is { Length: > 0 } id ? $"{node.Name}(\"{id}\")" : $"{node.Name}#{node.Ordinal}";
}

/// <summary>
/// A sub-part of a mechanism that gameplay moves: a door slab, a platform deck, a lever arm.
/// </summary>
/// <remarks>
/// The part's pose is set relative to the pose the level file put it in (its bind pose), in its
/// parent's frame, so <c>travel</c> and turn axes are read in the mechanism's own SCAD frame.
/// Its collision is made kinematic once, and every move drags the bodies along: a character
/// standing on a moving deck rides it instead of being left floating.
/// </remarks>
public sealed class MovingPart
{
    private readonly List<(uint NodeId, uint BodyId)> bodies = [];
    private readonly Action<uint>? onBody;
    private readonly Vec3 bindTranslation;
    private readonly Rot bindRotation;
    private readonly Vec3 bindScale;
    private bool bodiesBound;
    private bool visible = true;

    public MovingPart(LevelNode node, Action<uint>? onBody = null)
    {
        Node = node;
        this.onBody = onBody;
        NodeRef reference = new(node.Id);
        bindTranslation = reference.Translation.N();
        bindRotation = reference.Rotation.N();
        bindScale = reference.Scale.N();
    }

    public LevelNode Node { get; }

    public bool Collides => bodies.Count > 0;

    /// <summary>The bodies the part's geometry collides with, made kinematic on first use.</summary>
    private void EnsureBodies()
    {
        if (bodiesBound)
        {
            return;
        }
        bodiesBound = true;
        TryBind(Node);
        foreach (LevelNode child in Node.Descendants())
        {
            TryBind(child);
        }
    }

    private void TryBind(LevelNode node)
    {
        uint body = Scene.MakeNodeKinematic(node.Id);
        if (PhysicsBodyIds.IsValid(body))
        {
            bodies.Add((node.Id, body));
            onBody?.Invoke(body);
        }
    }

    /// <summary>
    /// Poses the part at its bind pose moved by <paramref name="offsetScad"/> (metres, mechanism's
    /// SCAD frame) and turned by <paramref name="degrees"/> around <paramref name="axisScad"/>.
    /// </summary>
    public void SetPose(Vec3 offsetScad, Vec3 axisScad, float degrees, float deltaSeconds)
    {
        EnsureBodies();
        Rot turn = degrees == 0.0f
            ? Rot.Identity
            : Rot.CreateFromAxisAngle(Vec3.Normalize(ScadSpace.ToEngine(axisScad)), degrees * (MathF.PI / 180.0f));
        Vec3 translation = bindTranslation + ScadSpace.ToEngine(offsetScad);
        Rot rotation = Rot.Normalize(turn * bindRotation);
        NodeTransform transform = new(Node.Id, translation.Gk(), rotation.Gk(), bindScale.Gk());
        Scene.SetNodeTransform(in transform);
        SyncBodies(deltaSeconds);
    }

    public void SetPose(Vec3 offsetScad, float deltaSeconds) => SetPose(offsetScad, Vec3.UnitZ, 0.0f, deltaSeconds);

    private void SyncBodies(float deltaSeconds)
    {
        float step = MathF.Max(deltaSeconds, 1.0f / 240.0f);
        foreach ((uint nodeId, uint bodyId) in bodies)
        {
            NodeTransform world = Scene.GetNodeWorldTransform(nodeId);
            Physics.MoveKinematicBody(bodyId, in world.Translation, in world.Rotation, step);
        }
    }

    /// <summary>Shows or hides the part; a hidden part also stops colliding.</summary>
    public void SetVisible(bool show)
    {
        if (visible == show)
        {
            return;
        }
        visible = show;
        EnsureBodies();
        Scene.SetNodeVisibleRecursive(Node.Id, show);
        foreach ((uint _, uint bodyId) in bodies)
        {
            Physics.SetBodyActive(bodyId, show);
        }
    }

    /// <summary>Is the character standing on one of this part's bodies?</summary>
    public bool CarriesBody(uint groundBodyId)
    {
        foreach ((uint _, uint bodyId) in bodies)
        {
            if (bodyId == groundBodyId)
            {
                return true;
            }
        }
        return false;
    }

    public Vec3 WorldPosition => Scene.GetNodeWorldTransform(Node.Id).Translation.N();

    public Bounds WorldBounds => Scene.GetNodeBounds(Node.Id, false);
}
