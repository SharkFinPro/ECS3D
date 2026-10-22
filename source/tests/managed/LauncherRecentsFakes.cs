using System;
using System.Collections.Generic;
using System.IO;
using ECS3DLauncher.Services;

namespace ECS3DManagedTests;

// A file system whose writes can fail a set number of times before succeeding.
internal sealed class FakeTreeFileSystem : IFileSystem
{
  private readonly HashSet<string> _files = [];
  private readonly Dictionary<string, string> _text = [];

  public bool FailWrites { get; set; }

  public int FailNextWrites { get; set; }

  public int Attempts { get; set; }

  public int Writes { get; private set; }

  public List<string> CreatedDirectories { get; } = [];

  public void AddFile(string path) => _files.Add(path);

  public void Seed(string path, string contents) => _text[path] = contents;

  public string? TextOf(string path) => _text.GetValueOrDefault(path);

  public bool FileExists(string path) => _files.Contains(path) || _text.ContainsKey(path);

  public bool DirectoryExists(string path) => false;

  public string ReadAllText(string path) =>
    _text.TryGetValue(path, out var text) ? text : throw new FileNotFoundException(path);

  public void WriteAllTextAtomic(string path, string contents)
  {
    Attempts++;
    if (FailNextWrites > 0)
    {
      FailNextWrites--;
      throw new IOException("write failed");
    }

    if (FailWrites)
    {
      throw new IOException("write failed");
    }

    Writes++;
    _text[path] = contents;
  }

  public void CreateDirectory(string path) => CreatedDirectories.Add(path);

  // This fake knows no folders.
  public IReadOnlyList<string> EnumerateDirectories(string path) => [];
}

internal sealed class FixedTimeProvider : TimeProvider
{
  public DateTimeOffset Now { get; set; } = new(2026, 10, 18, 19, 2, 11, TimeSpan.Zero);

  public override DateTimeOffset GetUtcNow() => Now;
}
