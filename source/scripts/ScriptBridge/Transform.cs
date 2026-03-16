using System;
using System.Runtime.InteropServices;

namespace ScriptBridge;

public unsafe class Transform
{
  private readonly IntPtr _uuid;

  internal Transform(string uuid)
  {
    _uuid = Marshal.StringToCoTaskMemUTF8(uuid);
  }

  ~Transform()
  {
    Marshal.FreeCoTaskMem(_uuid);
  }

  public (float x, float y, float z) position
  {
    get
    {
      float x = 0, y = 0, z = 0;
      NativeBindings.Transform.getPosition(_uuid, &x, &y, &z);
      return (x, y, z);
    }
  }

  public (float x, float y, float z) scale
  {
    get
    {
      float x = 0, y = 0, z = 0;
      NativeBindings.Transform.getScale(_uuid, &x, &y, &z);
      return (x, y, z);
    }
  }

  public (float x, float y, float z) rotation
  {
    get
    {
      float x = 0, y = 0, z = 0;
      NativeBindings.Transform.getRotation(_uuid, &x, &y, &z);
      return (x, y, z);
    }
  }

  public void setScale(float x, float y, float z) =>
    NativeBindings.Transform.setScale(_uuid, x, y, z);

  public void setRotation(float x, float y, float z) =>
    NativeBindings.Transform.setRotation(_uuid, x, y, z);

  public void move(float x, float y, float z) =>
    NativeBindings.Transform.move(_uuid, x, y, z);

  public void start() => NativeBindings.Transform.start(_uuid);

  public void stop() => NativeBindings.Transform.stop(_uuid);
}