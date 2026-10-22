using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading;
using ECS3DLauncher.Services;

namespace ECS3DManagedTests;

internal sealed class PropertyLog
{
  public PropertyLog(INotifyPropertyChanged source)
  {
    source.PropertyChanged += (_, args) => Names.Add(args.PropertyName ?? "");
  }

  public List<string> Names { get; } = [];
}

// A settings editor over an in-memory file system, with the engine folders the view model tests need.
internal sealed class LauncherRig
{
  public static readonly string LauncherDir = Path.GetFullPath(Path.Combine("vm-launcher", "dir"));
  public static readonly string OtherDir = Path.GetFullPath(Path.Combine("vm-other", "bin"));
  private const string Documents = "/docs";

  public static readonly string DefaultProjects = Path.Combine(Documents, "ECS3D Projects");

  public FakeFileSystem Files { get; } = new();

  public RecordingLauncherLog Log { get; } = new();

  public string SettingsPath => ShellFixtures.UserData.SettingsFile();

  public void AddEngine(string folder, params EngineExecutable[] skip)
  {
    foreach (var executable in Enum.GetValues<EngineExecutable>().Where(item => !skip.Contains(item)))
    {
      Files.Seed(Path.Combine(folder, EngineExecutableNames.FileName(executable, HostOs.Linux)), "");
    }
  }

  public SettingsEditor NewEditor()
  {
    var store = new LauncherSettingsStore(Files, Log, ShellFixtures.UserData, () => Documents);
    return new SettingsEditor(store, new EngineLocator(Files, HostOs.Linux, LauncherDir));
  }

  public JsonObject? Saved() =>
    Files.TextOf(SettingsPath) is { } text ? (JsonObject)JsonNode.Parse(text)! : null;
}

// Runs an action with no synchronization context, so a task continuation resumes inline on the thread that
// completes the task instead of being posted to the test runner's context.
internal static class NoContext
{
  public static void Run(Action action)
  {
    var previous = SynchronizationContext.Current;
    SynchronizationContext.SetSynchronizationContext(null);
    try
    {
      action();
    }
    finally
    {
      SynchronizationContext.SetSynchronizationContext(previous);
    }
  }
}
