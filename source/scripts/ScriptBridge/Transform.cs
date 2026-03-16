namespace ScriptBridge;

public unsafe class Transform
{
  public float positionX => NativeBindings.Transform.getPositionX();
}