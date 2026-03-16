namespace ScriptBridge;

public abstract class ScriptBase
{
  protected static Transform transform { get; } = new Transform();

  public virtual void start() {}
  public virtual void fixedUpdate(float dt) {}
  public virtual void variableUpdate() {}
  public virtual void stop() {}
}
