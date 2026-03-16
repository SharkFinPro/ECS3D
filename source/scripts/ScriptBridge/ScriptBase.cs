namespace ScriptBridge;

public abstract class ScriptBase
{
  public string EntityId { get; internal set; } = "";


  protected static Transform transform => _transform ??= new Transform("");
  private static Transform? _transform;

//   protected static Transform transform { get; private set; } = new Transform("");

//   internal void initComponents()
//   {
//     transform = new Transform(EntityId);
//   }

  public virtual void start() {}
  public virtual void fixedUpdate(float dt) {}
  public virtual void variableUpdate() {}
  public virtual void stop() {}
}
