using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

public sealed class SystemProcessRunner : IProcessRunner
{
    // Held for the life of the launcher: the OS closes the handle when the launcher dies, which kills the members.
    private static readonly Lazy<KillOnCloseJob> LauncherJob = new(() => new KillOnCloseJob());

    public IRunningProcess Start(ProcessLaunch launch)
    {
        var startInfo = new ProcessStartInfo(launch.FileName)
        {
            WorkingDirectory = launch.WorkingDirectory,
            UseShellExecute = false,
            CreateNoWindow = launch.HideWindow,
        };

        foreach (var argument in launch.Arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        try
        {
            var process = Process.Start(startInfo)
                ?? throw new ProcessStartException($"{launch.FileName} could not be started.");
            if (launch.EndWithLauncher)
            {
                TieToLauncher(process);
            }

            return new SystemRunningProcess(process);
        }
        catch (Exception exception) when (exception is Win32Exception or InvalidOperationException)
        {
            throw new ProcessStartException($"{launch.FileName} could not be started: {exception.Message}", exception);
        }
    }

    // Best effort: a process that cannot be tied still runs, and the launcher's own Stop paths still end it.
    private static void TieToLauncher(Process process)
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        try
        {
            LauncherJob.Value.Assign(process);
        }
        catch (Win32Exception)
        {
            // The process exited first, or the OS refused the job; it simply is not tied.
        }
    }

    private sealed class SystemRunningProcess : IRunningProcess
    {
        private readonly Process _process;

        public SystemRunningProcess(Process process)
        {
            _process = process;
        }

        private int? _finalExitCode;
        private bool _disposed;

        public bool HasExited => _disposed || _process.HasExited;

        public int? ExitCode
        {
            get
            {
                if (_disposed)
                {
                    return _finalExitCode;
                }

                return _process.HasExited ? _process.ExitCode : null;
            }
        }

        public async Task<int> WaitForExitAsync(CancellationToken cancellationToken)
        {
            await _process.WaitForExitAsync(cancellationToken);
            return _process.ExitCode;
        }

        public void Kill()
        {
            if (_disposed)
            {
                return;
            }

            try
            {
                _process.Kill(true);
            }
            catch (Exception exception)
                when (exception is InvalidOperationException or Win32Exception or AggregateException)
            {
                // The process exited first, or part of its tree already had; either way it is gone.
            }
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            if (_process.HasExited)
            {
                _finalExitCode = _process.ExitCode;
            }

            _disposed = true;
            _process.Dispose();
        }
    }
}
