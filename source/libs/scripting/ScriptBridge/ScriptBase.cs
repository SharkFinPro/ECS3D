using System.Collections.Generic;

namespace ScriptBridge;

public abstract class ScriptBase
{
    public string EntityId { get; internal set; } = "";

    protected RigidBody rigidBody { get; private set; } = null!;

    protected Transform transform { get; private set; } = null!;

    internal void initComponents()
    {
        rigidBody = new RigidBody(EntityId);
        transform = new Transform(EntityId);
    }

    // Reach another script by type. Returns null if the object has no script of type T (or no such
    // object). The no-argument overloads target this script's own object, for sibling-script access.
    protected T? getScript<T>(string uuid) where T : ScriptBase => Bridge.FindScript<T>(uuid);

    protected T? getScript<T>() where T : ScriptBase => Bridge.FindScript<T>(EntityId);

    // All scripts on an object, for the untyped case (e.g. broadcasting an intent to whatever is there).
    protected IReadOnlyList<ScriptBase> getScripts(string uuid) => Bridge.FindScripts(uuid);

    protected IReadOnlyList<ScriptBase> getScripts() => Bridge.FindScripts(EntityId);

    public virtual void start() {}
    public virtual void fixedUpdate(float dt) {}
    public virtual void variableUpdate() {}
    public virtual void stop() {}
}
