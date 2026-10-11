using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;
using static ECS3DManagedTests.FakeNatives;

namespace ECS3DManagedTests;

internal static unsafe class FakePlayerControllerNatives
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

  internal static PlayerControllerBindings Make() => new()
  {
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&PlayerHas,
    getPlayerSlot = &PlayerGetSlot,
    setPlayerSlot = &PlayerSetSlot
  };
}
