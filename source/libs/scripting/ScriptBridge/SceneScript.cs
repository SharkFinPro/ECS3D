namespace ScriptBridge;

// A script attached to a scene rather than an object, for scene-wide logic such as a round timer or a
// score. It has no object, so none of ScriptBase's per-object components or collision callbacks. Scene
// scripts start before object scripts, run before them each tick, and stop after them.
public abstract class SceneScript : ScriptInstance
{
    public string SceneId => OwnerId;
}
