using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using ECS3DLauncher.Services;

namespace ECS3DManagedTests;

internal sealed class FakeFileSystem : IFileSystem
{
  private readonly HashSet<string> _files;

  public FakeFileSystem(params string[] files)
  {
    _files = new HashSet<string>(files);
  }

  public bool FileExists(string path) => _files.Contains(path);

  public bool DirectoryExists(string path) => false;
}

internal sealed class FakeRunningProcess : IRunningProcess
{
  private readonly TaskCompletionSource<int> _exit = new();

  public bool HasExited => _exit.Task.IsCompleted;

  public int? ExitCode => _exit.Task.IsCompletedSuccessfully ? _exit.Task.Result : null;

  public bool Killed { get; private set; }

  public void Exit(int code) => _exit.SetResult(code);

  public Task<int> WaitForExitAsync(CancellationToken cancellationToken) => _exit.Task.WaitAsync(cancellationToken);

  public void Kill() => Killed = true;

  public void Dispose()
  {
  }
}

internal sealed class FakeProcessRunner : IProcessRunner
{
  public List<ProcessLaunch> Launches { get; } = [];

  public FakeRunningProcess? Last { get; private set; }

  public string? FailWith { get; set; }

  public IRunningProcess Start(ProcessLaunch launch)
  {
    if (FailWith is not null)
    {
      throw new ProcessStartException(FailWith);
    }

    Launches.Add(launch);
    Last = new FakeRunningProcess();
    return Last;
  }
}
