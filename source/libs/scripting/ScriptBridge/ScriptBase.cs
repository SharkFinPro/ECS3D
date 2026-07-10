using System.Collections.Generic;
using System.Numerics;

namespace ScriptBridge;

public abstract class ScriptBase
{
    public string EntityId { get; internal set; } = "";

    protected RigidBody rigidBody { get; private set; } = null!;

    protected Transform transform { get; private set; } = null!;

    // This script's own camera (read-only), for moving relative to where the view faces. Reads a forward
    // default (0,0,-1) if the object has no Camera.
    protected Camera camera { get; private set; } = null!;

    // This script's own player's input, resolved through its object's PlayerController. Reads as "nothing
    // pressed" if the object has no PlayerController. Prefer this over the global InputUtils, which reads
    // every player's input aggregated together.
    protected PlayerInput input { get; private set; } = null!;

    internal void initComponents()
    {
        rigidBody = new RigidBody(EntityId);
        transform = new Transform(EntityId);
        camera = new Camera(EntityId);
        input = new PlayerInput(EntityId);
    }

    // Reach another script by type. Returns null if the object has no script of type T (or no such
    // object). The no-argument overloads target this script's own object, for sibling-script access.
    protected T? getScript<T>(string uuid) where T : ScriptBase => Bridge.FindScript<T>(uuid);

    protected T? getScript<T>() where T : ScriptBase => Bridge.FindScript<T>(EntityId);

    // All scripts on an object, for the untyped case (e.g. broadcasting an intent to whatever is there).
    protected IReadOnlyList<ScriptBase> getScripts(string uuid) => Bridge.FindScripts(uuid);

    protected IReadOnlyList<ScriptBase> getScripts() => Bridge.FindScripts(EntityId);

    // Scene queries that automatically ignore this script's own object, so a ray/overlap cast from an
    // object never reports itself. Use these instead of the World.* versions unless you specifically want
    // self included (World.raycast/overlapSphere take an optional ignoreUuid too).
    protected bool raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit,
                           uint layerMask = 0xFFFFFFFF)
        => World.raycast(origin, direction, maxDistance, out hit, layerMask, EntityId);

    protected string[] overlapSphere(Vector3 center, float radius, uint layerMask = 0xFFFFFFFF)
        => World.overlapSphere(center, radius, layerMask, EntityId);

    public virtual void start() {}
    public virtual void fixedUpdate(float dt) {}
    public virtual void variableUpdate() {}
    public virtual void stop() {}

    // Contact events, dispatched by the server after each tick's collision pass. otherUuid is the object
    // this one is touching; onCollisionEnter fires the tick contact begins, onCollisionStay every tick it
    // persists, onCollisionExit the tick it ends (otherUuid may already be destroyed by then).
    public virtual void onCollisionEnter(string otherUuid) {}
    public virtual void onCollisionStay(string otherUuid) {}
    public virtual void onCollisionExit(string otherUuid) {}
}
