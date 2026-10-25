using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace ECS3DLauncher.Services;

// A Windows job object whose processes are all killed when its last handle closes. The OS closes the handle when
// the owning process dies however it dies, which is what ends a server the launcher can no longer stop itself.
public sealed class KillOnCloseJob : IDisposable
{
    private const int ExtendedLimitInformationClass = 9;
    private const uint KillOnJobCloseFlag = 0x2000;
    private const int LimitFlagsOffset = 16;

    private readonly IntPtr _handle;

    public KillOnCloseJob()
    {
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException("Job objects exist only on Windows.");
        }

        _handle = CreateJobObject(IntPtr.Zero, null);
        if (_handle == IntPtr.Zero)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        try
        {
            SetKillOnClose();
        }
        catch
        {
            CloseHandle(_handle);
            throw;
        }
    }

    public void Assign(Process process)
    {
        if (!AssignProcessToJobObject(_handle, process.Handle))
        {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }
    }

    public void Dispose()
    {
        CloseHandle(_handle);
    }

    // JOBOBJECT_EXTENDED_LIMIT_INFORMATION is written as raw bytes: only LimitFlags, at offset 16 on both
    // 32-bit and 64-bit Windows, is set, and the structure is 144 bytes on 64-bit and 112 on 32-bit.
    private void SetKillOnClose()
    {
        var size = IntPtr.Size == 8 ? 144 : 112;
        var buffer = Marshal.AllocHGlobal(size);
        try
        {
            for (var offset = 0; offset < size; offset++)
            {
                Marshal.WriteByte(buffer, offset, 0);
            }

            Marshal.WriteInt32(buffer, LimitFlagsOffset, unchecked((int)KillOnJobCloseFlag));
            if (!SetInformationJobObject(_handle, ExtendedLimitInformationClass, buffer, (uint)size))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateJobObject(IntPtr attributes, string? name);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetInformationJobObject(IntPtr job, int informationClass, IntPtr information, uint size);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CloseHandle(IntPtr handle);
}
