using System;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct InputUtilsBindings
{
    public delegate* unmanaged<int, bool> keyIsPressed;
}

public unsafe class InputUtils
{
    public bool keyIsPressed(int key) => NativeBindings.InputUtils.keyIsPressed(key);
}