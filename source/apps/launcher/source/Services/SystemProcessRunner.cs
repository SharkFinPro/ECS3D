using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

public sealed class SystemProcessRunner : IProcessRunner
{
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
            return new SystemRunningProcess(process);
        }
        catch (Exception exception) when (exception is Win32Exception or InvalidOperationException)
        {
            throw new ProcessStartException($"{launch.FileName} could not be started: {exception.Message}", exception);
        }
    }

    private sealed class SystemRunningProcess : IRunningProcess
    {
        private readonly Process _process;

        public SystemRunningProcess(Process process)
        {
            _process = process;
        }

        public bool HasExited => _process.HasExited;

        public int? ExitCode => _process.HasExited ? _process.ExitCode : null;

        public async Task<int> WaitForExitAsync(CancellationToken cancellationToken)
        {
            await _process.WaitForExitAsync(cancellationToken);
            return _process.ExitCode;
        }

        public void Kill()
        {
            if (!_process.HasExited)
            {
                _process.Kill(true);
            }
        }
    }
}
