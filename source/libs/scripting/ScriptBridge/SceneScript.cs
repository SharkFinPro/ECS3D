namespace ScriptBridge;

// A script attached to a scene rather than an object, for scene-wide logic such as a round timer or a
// score. It has no object, so none of ScriptBase's per-object components or collision callbacks. Scene
// scripts start before object scripts, run before them each tick, and stop after them.
public abstract class SceneScript : ScriptInstance
{
    public string SceneId => OwnerId;

    // Scene-wide events, delivered only while the scene runs. onSceneLoaded fires once after every script has
    // started, then onPlayerJoined for each player already connected. onSceneUnloading fires before any
    // script stops. Spawn and destroy events name one object each, a spawned subtree's descendants included.
    // A script started mid-run (added while running, or re-created by a hot reload) misses onSceneLoaded and
    // those start-time joins. A failed project load that restarts the scene delivers unloading, then loaded
    // again. An editor edit that fails part way reports no spawn or destroy events.
    public virtual void onSceneLoaded() {}

    public virtual void onSceneUnloading() {}

    public virtual void onObjectSpawned(string uuid) {}

    public virtual void onObjectDestroyed(string uuid) {}

    public virtual void onPlayerJoined(int slot) {}

    public virtual void onPlayerLeft(int slot) {}
}
