using System;
using System.Threading;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

public interface IRunningProcess : IDisposable
{
    bool HasExited { get; }

    int? ExitCode { get; }

    Task<int> WaitForExitAsync(CancellationToken cancellationToken);

    void Kill();
}
