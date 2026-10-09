using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;
using static ECS3DManagedTests.FakeNatives;

namespace ECS3DManagedTests;

internal static unsafe class FakeInputUtilsNatives
{
  [UnmanagedCallersOnly]
  private static byte InputKeyIsPressed(int key)
  {
    Rec("InputUtils.keyIsPressed", key);
    return Flag("InputUtils.keyIsPressed");
  }

  [UnmanagedCallersOnly]
  private static byte InputWindowIsFocused()
  {
    Rec("InputUtils.windowIsFocused");
    return Flag("InputUtils.windowIsFocused");
  }

  [UnmanagedCallersOnly]
  private static byte InputKeyIsPressedForObject(IntPtr u, int key)
  {
    Rec("InputUtils.keyIsPressedForObject", u, key);
    return Flag("InputUtils.keyIsPressedForObject");
  }

  [UnmanagedCallersOnly]
  private static byte InputWindowIsFocusedForObject(IntPtr u)
  {
    Rec("InputUtils.windowIsFocusedForObject", u);
    return Flag("InputUtils.windowIsFocusedForObject");
  }

  [UnmanagedCallersOnly]
  private static void InputMousePositionForObject(IntPtr u, float* x, float* y)
  {
    Rec("InputUtils.mousePositionForObject", u);
    OutVec2("InputUtils.mousePositionForObject", x, y);
  }

  [UnmanagedCallersOnly]
  private static void InputMouseDeltaForObject(IntPtr u, float* x, float* y)
  {
    Rec("InputUtils.mouseDeltaForObject", u);
    OutVec2("InputUtils.mouseDeltaForObject", x, y);
  }

  [UnmanagedCallersOnly]
  private static float InputScrollForObject(IntPtr u)
  {
    Rec("InputUtils.scrollForObject", u);
    return Get<float>("InputUtils.scrollForObject");
  }

  [UnmanagedCallersOnly]
  private static byte InputMouseButtonForObject(IntPtr u, int button)
  {
    Rec("InputUtils.mouseButtonForObject", u, button);
    return Flag("InputUtils.mouseButtonForObject");
  }

  [UnmanagedCallersOnly]
  private static byte InputWasKeyPressedThisTickForObject(IntPtr u, int key)
  {
    Rec("InputUtils.wasKeyPressedThisTickForObject", u, key);
    return Flag("InputUtils.wasKeyPressedThisTickForObject");
  }

  [UnmanagedCallersOnly]
  private static byte InputWasKeyReleasedThisTickForObject(IntPtr u, int key)
  {
    Rec("InputUtils.wasKeyReleasedThisTickForObject", u, key);
    return Flag("InputUtils.wasKeyReleasedThisTickForObject");
  }

  internal static InputUtilsBindings Make() => new()
  {
    keyIsPressed = (delegate* unmanaged<int, bool>)(void*)(delegate* unmanaged<int, byte>)&InputKeyIsPressed,
    windowIsFocused = (delegate* unmanaged<bool>)(void*)(delegate* unmanaged<byte>)&InputWindowIsFocused,
    keyIsPressedForObject = (delegate* unmanaged<IntPtr, int, bool>)(void*)
      (delegate* unmanaged<IntPtr, int, byte>)&InputKeyIsPressedForObject,
    windowIsFocusedForObject = (delegate* unmanaged<IntPtr, bool>)(void*)
      (delegate* unmanaged<IntPtr, byte>)&InputWindowIsFocusedForObject,
    mousePositionForObject = &InputMousePositionForObject,
    mouseDeltaForObject = &InputMouseDeltaForObject,
    scrollForObject = &InputScrollForObject,
    mouseButtonForObject = (delegate* unmanaged<IntPtr, int, bool>)(void*)
      (delegate* unmanaged<IntPtr, int, byte>)&InputMouseButtonForObject,
    wasKeyPressedThisTickForObject = (delegate* unmanaged<IntPtr, int, bool>)(void*)
      (delegate* unmanaged<IntPtr, int, byte>)&InputWasKeyPressedThisTickForObject,
    wasKeyReleasedThisTickForObject = (delegate* unmanaged<IntPtr, int, bool>)(void*)
      (delegate* unmanaged<IntPtr, int, byte>)&InputWasKeyReleasedThisTickForObject
  };
}
