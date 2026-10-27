using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using ECS3DLauncher.Services;

namespace ECS3DManagedTests;

// A tree of text files and folders in memory that can copy, move and delete, with separators folded to '/'.
internal sealed class MemoryTreeFileSystem : IFileSystem
{
  private readonly Dictionary<string, string> _files = [];
  private readonly HashSet<string> _directories = [];

  // Thrown when a file whose name ends with CopyFailureSuffix is copied.
  public Exception? CopyFailure { get; set; }

  // Called after each file is copied.
  public Action<string>? AfterCopy { get; set; }

  public string CopyFailureSuffix { get; set; } = "A.cs";

  // Looking at this folder or anything below it throws, as an unreadable share would.
  public string? UnreadableUnder { get; set; }

  private static string Norm(string path)
  {
    var folded = path.Replace('\\', '/');
    return folded.Length > 1 ? folded.TrimEnd('/') : folded;
  }

  private static string Parent(string path)
  {
    var cut = path.LastIndexOf('/');
    return cut < 0 ? "" : cut == 0 ? "/" : path[..cut];
  }

  private string Seen(string path)
  {
    var folded = Norm(path);
    if (UnreadableUnder is { } blocked && (folded == Norm(blocked) || folded.StartsWith(Norm(blocked) + "/", StringComparison.Ordinal)))
    {
      throw new UnauthorizedAccessException("cannot look here");
    }

    return folded;
  }

  public void AddDirectory(string path)
  {
    for (var folder = Norm(path); folder.Length > 0 && _directories.Add(folder); folder = Parent(folder))
    {
      // Each ancestor is added until one is already known.
    }
  }

  // Makes a drive or share vanish while the folders below it stay recorded, as an unplugged one would look.
  public void RemoveRoot(string root) => _directories.Remove(Norm(root));

  public void Seed(string path, string contents)
  {
    _files[Norm(path)] = contents;
    AddDirectory(Parent(Norm(path)));
  }

  public bool FileExists(string path) => _files.ContainsKey(Seen(path));

  public bool DirectoryExists(string path) => _directories.Contains(Seen(path));

  public IReadOnlyList<string> EnumerateDirectories(string path) =>
    _directories.Where(directory => Parent(directory) == Norm(path)).Order(StringComparer.Ordinal).ToList();

  public IReadOnlyList<string> EnumerateFiles(string path) =>
    _files.Keys.Where(file => Parent(file) == Norm(path)).Order(StringComparer.Ordinal).ToList();

  public string ReadAllText(string path) =>
    _files.TryGetValue(Norm(path), out var text) ? text : throw new FileNotFoundException(path);

  public void WriteAllTextAtomic(string path, string contents) => Seed(path, contents);

  public void CreateDirectory(string path) => AddDirectory(path);

  public void CopyFile(string source, string destination)
  {
    if (CopyFailure is not null && Norm(source).EndsWith(CopyFailureSuffix, StringComparison.Ordinal))
    {
      throw CopyFailure;
    }

    if (_files.ContainsKey(Norm(destination)))
    {
      throw new IOException("exists");
    }

    Seed(destination, ReadAllText(source));
    AfterCopy?.Invoke(Norm(destination));
  }

  public void MoveDirectory(string source, string destination)
  {
    var from = Norm(source);
    var to = Norm(destination);
    if (_directories.Contains(to))
    {
      throw new IOException("exists");
    }

    foreach (var directory in _directories.Where(item => item == from || item.StartsWith(from + "/", StringComparison.Ordinal)).ToList())
    {
      _directories.Remove(directory);
      _directories.Add(to + directory[from.Length..]);
    }

    foreach (var file in _files.Keys.Where(item => item.StartsWith(from + "/", StringComparison.Ordinal)).ToList())
    {
      _files[to + file[from.Length..]] = _files[file];
      _files.Remove(file);
    }

    AddDirectory(Parent(to));
  }

  public void DeleteDirectory(string path, bool recursive)
  {
    var folder = Norm(path);
    var inside = _directories.Where(item => item.StartsWith(folder + "/", StringComparison.Ordinal)).ToList();
    var held = _files.Keys.Where(item => item.StartsWith(folder + "/", StringComparison.Ordinal)).ToList();
    if (!recursive && (inside.Count > 0 || held.Count > 0))
    {
      throw new IOException("not empty");
    }

    inside.ForEach(item => _directories.Remove(item));
    held.ForEach(item => _files.Remove(item));
    _directories.Remove(folder);
  }
}
