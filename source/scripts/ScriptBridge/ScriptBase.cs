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

  public virtual void start() {}
  public virtual void fixedUpdate(float dt) {}
  public virtual void variableUpdate() {}
  public virtual void stop() {}
}
