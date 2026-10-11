using System;
using System.Collections.Generic;

namespace ScriptBridge;

// What object scripts (ScriptBase) and scene scripts (SceneScript) share: the lifecycle, timers and events.
// Only those two derive from it; the constructor is internal so user scripts pick one of them.
public abstract class ScriptInstance
{
    private readonly Scheduler _timers = new();
    private readonly HashSet<Type> _reportedMissingSceneScripts = new();

    internal ScriptInstance()
    {
    }

    // The uuid of the owner this instance is registered under: the object for a ScriptBase, the scene for a
    // SceneScript. Assigned by Bridge.AddInstance before the instance is registered.
    internal string OwnerId { get; set; } = "";

    // Run an action once after the given seconds, or repeatedly every that many seconds. Time advances
    // only on fixed ticks, so timers freeze while the scene is paused or stopped. The returned handle can
    // cancel the timer; an invalid duration schedules nothing and returns an inactive handle.
    protected TimerHandle after(float seconds, Action action) => _timers.after(seconds, action);

    protected TimerHandle every(float seconds, Action action) => _timers.every(seconds, action);

    internal void tickTimers(float dt) => _timers.tick(dt);

    // Named events between scripts. Delivery is synchronous, each handler runs under its own script's fault
    // gate, and subscriptions end with this instance. Only a live instance can subscribe, so a constructor
    // call (the owner id is assigned after construction) or one from a detached instance is refused with an
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

    // The current scene's scene script of type T, or null when there is none or it has faulted.
    protected T? getSceneScript<T>() where T : SceneScript => Bridge.FindSceneScript<T>();

    // Like getSceneScript, but logs one error per caller instance and type when it is missing.
    protected T? requireSceneScript<T>() where T : SceneScript
    {
        var found = Bridge.FindSceneScript<T>();
        if (found == null && _reportedMissingSceneScripts.Add(typeof(T)))
        {
            Log.error($"Script '{GetType().Name}' requires scene script '{typeof(T).Name}', " +
                      "but the current scene has none (or it has faulted).");
        }

        return found;
    }

    private string? OwnerKeyIfLive(string eventName)
    {
        var className = GetType().Name;
        if (Bridge.IsLive(OwnerId, className, this))
        {
            return Bridge.Key(OwnerId, className);
        }

        Log.warn($"Script '{className}' tried to subscribe to event '{eventName}' while not attached; " +
                 "subscribe from start().");
        return null;
    }

    public virtual void start() {}
    public virtual void fixedUpdate(float dt) {}
    public virtual void variableUpdate() {}
    public virtual void stop() {}
}
