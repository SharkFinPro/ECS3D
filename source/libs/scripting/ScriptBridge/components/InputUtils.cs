using System;
using System.Runtime.InteropServices;

namespace ScriptBridge;

public enum Key
{
    RIGHT  = 262,
    LEFT   = 263,
    DOWN   = 264,
    UP     = 265,
    
    A = 65, B = 66, C = 67, D = 68, E = 69, F = 70,
    G = 71, H = 72, I = 73, J = 74, K = 75, L = 76,
    M = 77, N = 78, O = 79, P = 80, Q = 81, R = 82,
    S = 83, T = 84, U = 85, V = 86, W = 87, X = 88,
    Y = 89, Z = 90,
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct InputUtilsBindings
{
    public delegate* unmanaged<int, bool> keyIsPressed;
    public delegate* unmanaged<bool> windowIsFocused;
    // Per-player: resolve the object's PlayerController.playerSlot and read that player's input. New
    // fields go at the END to keep the layout matched with the native InputUtilsBindings struct.
    public delegate* unmanaged<IntPtr, int, bool> keyIsPressedForObject;
    public delegate* unmanaged<IntPtr, bool> windowIsFocusedForObject;
}

// Player-agnostic input: reads the aggregate across all players (a key is pressed if any player presses
// it). For a specific player's input, use ScriptBase.input (backed by PlayerInput) instead.
public static unsafe class InputUtils
{
    private static bool keyIsPressed(int key) => NativeBindings.InputUtils.keyIsPressed(key);

    public static bool keyIsPressed(Key key) => keyIsPressed((int)key);

    public static bool windowIsFocused() => NativeBindings.InputUtils.windowIsFocused();
}

// A single player's input, scoped to the object it was constructed from: it reads only that object's
// player slot (via its PlayerController). ScriptBase exposes one as `input`, bound to the script's own
// object, so `input.keyIsPressed(Key.W)` reads this player and no other. Reads as "nothing pressed" when
// the object has no PlayerController.
public sealed unsafe class PlayerInput
{
    private readonly IntPtr _uuid;

    internal PlayerInput(string uuid)
    {
        _uuid = Marshal.StringToCoTaskMemUTF8(uuid);
    }

    ~PlayerInput()
    {
        Marshal.FreeCoTaskMem(_uuid);
    }

    public bool keyIsPressed(Key key) => NativeBindings.InputUtils.keyIsPressedForObject(_uuid, (int)key);

    public bool windowIsFocused() => NativeBindings.InputUtils.windowIsFocusedForObject(_uuid);
}