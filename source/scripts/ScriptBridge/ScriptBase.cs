namespace ScriptBridge;

public abstract class ScriptBase
{
    public virtual void OnStart() { }
    public virtual void OnUpdate(float dt) { }
    public virtual void OnStop() { }
}
