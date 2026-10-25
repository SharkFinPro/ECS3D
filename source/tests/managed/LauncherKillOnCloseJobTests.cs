using System;
using System.Diagnostics;
using System.Linq;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherKillOnCloseJobTests
{
  private static readonly TimeSpan Patience = TimeSpan.FromSeconds(15);

  private static Process StartLongRunning()
  {
    var info = new ProcessStartInfo("ping", "-n 120 127.0.0.1")
    {
      UseShellExecute = false,
      CreateNoWindow = true,
      RedirectStandardOutput = true,
    };
    return Process.Start(info)!;
  }

  private static void KillQuietly(Process process)
  {
    try
    {
      process.Kill(true);
    }
    catch (InvalidOperationException)
    {
      // Already gone.
    }
  }

  [Fact]
  public void ClosingTheJobKillsOnlyTheProcessesAssignedToIt()
  {
    if (!OperatingSystem.IsWindows())
    {
      Assert.Throws<PlatformNotSupportedException>(() => new KillOnCloseJob());
      return;
    }

    using var tied = StartLongRunning();
    using var free = StartLongRunning();
    try
    {
      using (var job = new KillOnCloseJob())
      {
        job.Assign(tied);
        Assert.False(tied.HasExited);
      }

      Assert.True(tied.WaitForExit(Patience));
      Assert.False(free.HasExited);
    }
    finally
    {
      KillQuietly(tied);
      KillQuietly(free);
    }
  }

  [Fact]
  public void AssigningADeadProcessFails()
  {
    if (!OperatingSystem.IsWindows())
    {
      return;
    }

    using var process = StartLongRunning();
    KillQuietly(process);
    Assert.True(process.WaitForExit(Patience));

    using var job = new KillOnCloseJob();
    Assert.ThrowsAny<System.ComponentModel.Win32Exception>(() => job.Assign(process));
  }

  [Fact]
  public void OnlyTheServerEndsWithTheLauncher()
  {
    var runner = new FakeProcessRunner();
    var root = System.IO.Path.GetFullPath("engine");
    var files = new FakeFileSystem(new[] { "ECS3DEditor", "ECS3DClient", "ECS3DServer" }
      .Select(name => System.IO.Path.Combine(root, name))
      .ToArray());
    var location = new EngineLocator(files, HostOs.Linux, System.IO.Path.GetFullPath("launcher")).Locate(root);
    var launcher = new EngineLauncher(runner, location);

    launcher.OpenProject("/p", new LaunchOptions(false, false));
    launcher.ExploreEngine(new LaunchOptions(false, false));
    launcher.PlayProject("/p");
    launcher.JoinGame("127.0.0.1", 3001);
    launcher.StartServer("/p", 3001);

    Assert.Equal(5, runner.Launches.Count);
    Assert.All(runner.Launches.GetRange(0, 4), launch => Assert.False(launch.EndWithLauncher));
    Assert.True(runner.Launches[4].EndWithLauncher);
  }
}
