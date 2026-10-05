using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;

namespace ECS3DManagedTests;

internal static unsafe partial class FakeNatives
{
  [UnmanagedCallersOnly]
  private static byte PlayerHas(IntPtr u)
  {
    Rec("PlayerController.has", u);
    return Flag("PlayerController.has");
  }

  [UnmanagedCallersOnly]
  private static int PlayerGetSlot(IntPtr u)
  {
    Rec("PlayerController.getPlayerSlot", u);
    return Get<int>("PlayerController.getPlayerSlot");
  }

  [UnmanagedCallersOnly]
  private static void PlayerSetSlot(IntPtr u, int slot) => Rec("PlayerController.setPlayerSlot", u, slot);

  private static PlayerControllerBindings MakePlayerController() => new()
  {
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&PlayerHas,
    getPlayerSlot = &PlayerGetSlot,
    setPlayerSlot = &PlayerSetSlot
  };
}
