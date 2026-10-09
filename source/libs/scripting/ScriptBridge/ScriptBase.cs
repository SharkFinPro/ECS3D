using System;
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

    private readonly Scheduler _timers = new();

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

    // Run an action once after the given seconds, or repeatedly every that many seconds. Time advances
    // only on fixed ticks, so timers freeze while the scene is paused or stopped. The returned handle can
    // cancel the timer; an invalid duration schedules nothing and returns an inactive handle.
    protected TimerHandle after(float seconds, Action action) => _timers.after(seconds, action);

    protected TimerHandle every(float seconds, Action action) => _timers.every(seconds, action);

    internal void tickTimers(float dt) => _timers.tick(dt);

    // Reach another script by type. Returns null if the object has no script of type T (or no such
    // object). The no-argument overloads target this script's own object, for sibling-script access.
    protected T? getScript<T>(string uuid) where T : ScriptBase => Bridge.FindScript<T>(uuid);

    protected T? getScript<T>() where T : ScriptBase => Bridge.FindScript<T>(EntityId);

    // All scripts on an object, for the untyped case (e.g. broadcasting an intent to whatever is there).
    protected IReadOnlyList<ScriptBase> getScripts(string uuid) => Bridge.FindScripts(uuid);

    protected IReadOnlyList<ScriptBase> getScripts() => Bridge.FindScripts(EntityId);

    // Scene queries that automatically ignore this script's own object and the colliders of its own body
    // (a compound body's child colliders), so a ray/overlap cast from an object does not report itself.
    // Use these instead of the World.* versions unless you specifically want
    // self included (World.raycast/overlapSphere take an optional ignoreUuid too).
    protected bool raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit,
                           uint layerMask = 0xFFFFFFFF)
        => World.raycast(origin, direction, maxDistance, out hit, layerMask, EntityId);

    protected string[] overlapSphere(Vector3 center, float radius, uint layerMask = 0xFFFFFFFF)
        => World.overlapSphere(center, radius, layerMask, EntityId);

    // Named events between scripts. Delivery is synchronous, each handler runs under its own script's fault
    // gate, and subscriptions end with this instance. Only a live instance can subscribe, so a constructor
    // call (EntityId is assigned after construction) or one from a detached instance is refused with an
    // inert handle; subscribe from start() on.
    protected Subscription subscribe(string eventName, Action<object?> handler)
        => OwnerKeyIfLive(eventName) is { } owner
            ? Bridge.Events.subscribe(owner, eventName, handler)
            : new Subscription(null, "", eventName ?? "", null);

    // Delivers only payloads that are a T; a mismatched payload is skipped with one warning.
    protected Subscription subscribe<T>(string eventName, Action<T> handler)
        => OwnerKeyIfLive(eventName) is { } owner
            ? Bridge.Events.subscribe<T>(owner, eventName, handler)
            : new Subscription(null, "", eventName ?? "", null);

    // Returns how many handlers ran.
    protected int publish(string eventName, object? payload = null)
        => Bridge.Events.publish(eventName, payload);

    private string? OwnerKeyIfLive(string eventName)
    {
        var className = GetType().Name;
        if (Bridge.IsLive(EntityId, className, this))
        {
            return Bridge.Key(EntityId, className);
        }

        Log.warn($"Script '{className}' tried to subscribe to event '{eventName}' while not attached to an " +
                 "object; subscribe from start().");
        return null;
    }

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
