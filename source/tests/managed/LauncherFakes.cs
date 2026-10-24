using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using ECS3DLauncher.Services;

namespace ECS3DManagedTests;

internal sealed class FakeFileSystem : IFileSystem
{
  private readonly HashSet<string> _files;
  private readonly Dictionary<string, string> _text = [];
  private readonly HashSet<string> _directories = [];

  public FakeFileSystem(params string[] files)
  {
    _files = new HashSet<string>(files);
  }

  public List<string> CreatedDirectories { get; } = [];

  public bool FailReads { get; set; }

  public bool FailEnumerate { get; set; }

  public bool FailWrites { get; set; }

  public bool FailMoves { get; set; }

  public bool FileExists(string path) => _files.Contains(path) || _text.ContainsKey(path);

  // Like the real file system, a trailing separator still names the folder.
  public bool DirectoryExists(string path) =>
    _directories.Contains(path) || (path.Length > 1 && _directories.Contains(path.TrimEnd('/', '\\')));

  public IReadOnlyList<string> EnumerateDirectories(string path)
  {
    if (FailEnumerate)
    {
      throw new UnauthorizedAccessException("access denied");
    }

    return _directories.Where(directory => Path.GetDirectoryName(directory) == path).Order(StringComparer.Ordinal).ToList();
  }

  public void AddDirectory(string path) => _directories.Add(path);

  public string ReadAllText(string path)
  {
    if (FailReads)
    {
      throw new IOException("read failed");
    }

    return _text.TryGetValue(path, out var text) ? text : throw new FileNotFoundException(path);
  }

  // A failed write or move leaves the target as it was, as the contract says.
  public void WriteAllTextAtomic(string path, string contents)
  {
    if (FailWrites)
    {
      throw new IOException("write failed");
    }

    if (FailMoves)
    {
      throw new IOException("move failed");
    }

    _text[path] = contents;
  }

  public IReadOnlyList<string> EnumerateFiles(string path) => [];

  public void CopyFile(string source, string destination) => throw new NotSupportedException();

  public void MoveDirectory(string source, string destination) => throw new NotSupportedException();

  public void DeleteDirectory(string path, bool recursive) => throw new NotSupportedException();

  public void CreateDirectory(string path) => CreatedDirectories.Add(path);

  public void Seed(string path, string contents) => _text[path] = contents;

  public string? TextOf(string path) => _text.GetValueOrDefault(path);
}

internal sealed class RecordingLauncherLog : ILauncherLog
{
  public List<string> Warnings { get; } = [];

  public void Warn(string message) => Warnings.Add(message);
}

internal sealed class FakeRunningProcess : IRunningProcess
{
  private readonly TaskCompletionSource<int> _exit = new();
  private int _disposeCount;

  public bool HasExited => _exit.Task.IsCompleted;

  public int? ExitCode => _exit.Task.IsCompletedSuccessfully ? _exit.Task.Result : null;

  public bool Killed { get; private set; }

  public int DisposeCount => Volatile.Read(ref _disposeCount);

  public void Exit(int code) => _exit.SetResult(code);

  public Task<int> WaitForExitAsync(CancellationToken cancellationToken) => _exit.Task.WaitAsync(cancellationToken);

  public void Kill() => Killed = true;

  public void Dispose() => Interlocked.Increment(ref _disposeCount);
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
