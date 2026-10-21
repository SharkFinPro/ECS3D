using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using ECS3DLauncher.Services;

namespace ECS3DManagedTests;

internal sealed class NoticeRecorder
{
  public List<LaunchNotice> Notices { get; } = [];

  public void Record(LaunchNotice notice) => Notices.Add(notice);
}

internal static class ShellFixtures
{
  public static readonly string EngineFolder = Path.GetFullPath(Path.Combine("shell-engine", "bin"));

  public static readonly LaunchOptions NoOptions = new(false, false);

  public static readonly UserDataDirectory UserData =
    new(HostOs.Linux, name => name == "XDG_CONFIG_HOME" ? "/cfg" : null);

  public static string ExpectedLog(string stem) => Path.Combine("/cfg", "ECS3D", "logs", stem + ".log");

  // An engine folder holding every executable except the ones named.
  public static EngineLocation EngineWithout(params EngineExecutable[] missing)
  {
    var present = Enum.GetValues<EngineExecutable>()
      .Where(executable => !missing.Contains(executable))
      .Select(executable => Path.Combine(EngineFolder, EngineExecutableNames.FileName(executable, HostOs.Linux)))
      .ToArray();
    return new EngineLocator(new FakeFileSystem(present), HostOs.Linux, EngineFolder).Locate(null);
  }

  public static LaunchCoordinator CoordinatorFor(
    EngineLocation engine,
    FakeProcessRunner runner,
    TimeSpan window,
    NoticeRecorder recorder) =>
    new(new EngineLauncher(runner, engine), new EarlyExitWatcher(UserData, window), recorder.Record);
}
