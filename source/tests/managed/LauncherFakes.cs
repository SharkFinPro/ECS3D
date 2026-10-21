using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using ECS3DLauncher.Services;

namespace ECS3DManagedTests;

internal sealed class FakeFileSystem : IFileSystem
{
  private readonly HashSet<string> _files;
  private readonly Dictionary<string, string> _text = [];

  public FakeFileSystem(params string[] files)
  {
    _files = new HashSet<string>(files);
  }

  public List<string> CreatedDirectories { get; } = [];

  public bool FailReads { get; set; }

  public bool FailWrites { get; set; }

  public bool FailMoves { get; set; }

  public bool FileExists(string path) => _files.Contains(path) || _text.ContainsKey(path);

  public bool DirectoryExists(string path) => false;

  public string ReadAllText(string path)
  {
    if (FailReads)
    {
      throw new IOException("read failed");
    }

    return _text.TryGetValue(path, out var text) ? text : throw new FileNotFoundException(path);
  }

  // A failed move removes the temp file, as the contract says, and leaves the target as it was.
  public void WriteAllTextAtomic(string path, string contents)
  {
    if (FailWrites)
    {
      throw new IOException("write failed");
    }

    var temp = path + ".tmp";
    _text[temp] = contents;
    _text.Remove(temp);
    if (FailMoves)
    {
      throw new IOException("move failed");
    }

    _text[path] = contents;
  }

  public void CreateDirectory(string path) => CreatedDirectories.Add(path);

  public void Seed(string path, string contents) => _text[path] = contents;

  public string? TextOf(string path) => _text.GetValueOrDefault(path);

  public bool HasTemp(string path) => _text.ContainsKey(path + ".tmp");
}

internal sealed class RecordingLauncherLog : ILauncherLog
{
  public List<string> Warnings { get; } = [];

  public void Warn(string message) => Warnings.Add(message);
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
    // A fake process holds no handle to release.
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
