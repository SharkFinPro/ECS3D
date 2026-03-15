namespace ScriptBridge;

public abstract class ScriptBase
{
  public virtual void start() {}
  public virtual void fixedUpdate(float dt) {}
  public virtual void variableUpdate() {}
  public virtual void stop() {}
}
