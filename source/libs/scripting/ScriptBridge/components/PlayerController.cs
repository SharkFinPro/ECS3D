using System;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct PlayerControllerBindings
{
    public delegate* unmanaged<IntPtr, bool> has;
    public delegate* unmanaged<IntPtr, int> getPlayerSlot;
    public delegate* unmanaged<IntPtr, int, void> setPlayerSlot;
}

// Marks an object as owned by a player and names which player slot it belongs to. Reached via
// World.tryGetPlayerController like Transform/ModelRenderer - including for a script's own object, by
// passing ScriptBase.EntityId - since a script's own player input is normally read through
// ScriptBase.input rather than this wrapper.
public unsafe class PlayerController
{
    private readonly IntPtr _uuid;

    internal PlayerController(string uuid)
    {
        _uuid = Marshal.StringToCoTaskMemUTF8(uuid);
    }

    ~PlayerController()
    {
        Marshal.FreeCoTaskMem(_uuid);
    }

    public int getPlayerSlot() => NativeBindings.PlayerController.getPlayerSlot(_uuid);

    public void setPlayerSlot(int playerSlot) => NativeBindings.PlayerController.setPlayerSlot(_uuid, playerSlot);
}
