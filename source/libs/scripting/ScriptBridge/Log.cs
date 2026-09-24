using System;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct LogBindings
{
    public delegate* unmanaged<int, IntPtr, void> write;
}

// Logs through the engine's ECS3DLog sink, under LogCategory::script. Falls back to Console.Out/Error
// when the binding hasn't been registered yet (e.g. a call made before ScriptEngine::registerBindings
// runs), so a message is never silently lost.
public static unsafe class Log
{
    public static void trace(string message) => write(0, message, toError: false);

    public static void debug(string message) => write(1, message, toError: false);

    public static void info(string message) => write(2, message, toError: false);

    public static void warn(string message) => write(3, message, toError: true);

    public static void error(string message) => write(4, message, toError: true);

    private static void write(int level, string message, bool toError)
    {
        if (NativeBindings.Log.write == null)
        {
            writeFallback(message, toError);
            return;
        }

        var messagePtr = Marshal.StringToCoTaskMemUTF8(message);
        try
        {
            NativeBindings.Log.write(level, messagePtr);
        }
        finally
        {
            Marshal.FreeCoTaskMem(messagePtr);
        }
    }

    // Same never-throws guarantee as Bridge.SafeWriteError: the console can itself throw (a closed or
    // redirected stream), and that must not escape a logging call.
    private static void writeFallback(string message, bool toError)
    {
        try
        {
            if (toError)
            {
                Console.Error.WriteLine(message);
            }
            else
            {
                Console.WriteLine(message);
            }
        }
        catch
        {
        }
    }
}
