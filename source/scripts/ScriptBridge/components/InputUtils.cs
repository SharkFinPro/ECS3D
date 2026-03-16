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
}

public static unsafe class InputUtils
{
    private static bool keyIsPressed(int key) => NativeBindings.InputUtils.keyIsPressed(key);

    public static bool keyIsPressed(Key key) => keyIsPressed((int)key);

    public static bool windowIsFocused() => NativeBindings.InputUtils.windowIsFocused();
}