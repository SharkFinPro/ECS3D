using System;
using System.Collections.Generic;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherUnhandledErrorTests
{
  private const string LogPath = "logs/launcher.log";

  private readonly RecordingLauncherLog _log = new();
  private readonly List<LaunchNotice> _notices = [];

  private UnhandledErrorPolicy NewPolicy(bool withNotice = true)
  {
    var policy = new UnhandledErrorPolicy(_log, LogPath);
    if (withNotice)
    {
      policy.ShowNotice = _notices.Add;
    }

    return policy;
  }

  private static InvalidOperationException ThrownFrom(string message)
  {
    try
    {
      throw new InvalidOperationException(message);
    }
    catch (InvalidOperationException ex)
    {
      return ex;
    }
  }

  [Fact]
  public void AUiExceptionIsLoggedWithItsDetailsAndRequestsOneNotice()
  {
    var policy = NewPolicy();

    var handled = policy.HandleUiException(ThrownFrom("boom"));

    Assert.True(handled);
    var line = Assert.Single(_log.Errors);
    Assert.Contains("InvalidOperationException", line);
    Assert.Contains("boom", line);
    Assert.Contains(nameof(ThrownFrom), line);
    var notice = Assert.Single(_notices);
    Assert.Equal(UnhandledErrorPolicy.ToastMessage, notice.Message);
    Assert.Equal(LogPath, notice.LogPath);
  }

  [Fact]
  public void WithoutANoticeTargetTheExceptionIsLoggedButNotHandled()
  {
    var policy = NewPolicy(withNotice: false);

    var handled = policy.HandleUiException(new InvalidOperationException("early"));

    Assert.False(handled);
    var line = Assert.Single(_log.Errors);
    Assert.Contains("early", line);
    Assert.Empty(_notices);
  }

  [Fact]
  public void AnExceptionRaisedWhileTheNoticeIsShownDoesNotNotifyAgain()
  {
    var policy = NewPolicy();
    var innerHandled = false;
    var reentered = false;
    policy.ShowNotice = notice =>
    {
      _notices.Add(notice);
      if (!reentered)
      {
        reentered = true;
        innerHandled = policy.HandleUiException(new InvalidOperationException("second"));
      }
    };

    var outerHandled = policy.HandleUiException(new InvalidOperationException("first"));

    Assert.True(outerHandled);
    Assert.True(innerHandled);
    Assert.Equal(2, _log.Errors.Count);
    Assert.Contains("second", _log.Errors[1]);
    Assert.Single(_notices);

    policy.HandleUiException(new InvalidOperationException("third"));

    Assert.Equal(2, _notices.Count);
  }

  [Fact]
  public void AFailingNoticeIsLoggedAndTheNextExceptionStillNotifies()
  {
    var policy = NewPolicy();
    policy.ShowNotice = _ => throw new InvalidOperationException("window gone");

    var handled = policy.HandleUiException(new InvalidOperationException("original"));

    Assert.True(handled);
    Assert.Equal(2, _log.Errors.Count);
    Assert.Contains("original", _log.Errors[0]);
    Assert.Contains("Could not show the error notice", _log.Errors[1]);
    Assert.Contains("window gone", _log.Errors[1]);

    policy.ShowNotice = _notices.Add;
    policy.HandleUiException(new InvalidOperationException("later"));

    Assert.Single(_notices);
  }

  [Fact]
  public void ABackgroundExceptionIsLoggedWithItsSourceAndNeverNotifies()
  {
    var policy = NewPolicy();

    policy.LogBackgroundException("AppDomain", new InvalidOperationException("worker failed"));

    var line = Assert.Single(_log.Errors);
    Assert.Contains("AppDomain", line);
    Assert.Contains("worker failed", line);
    Assert.Empty(_notices);

    policy.HandleUiException(new InvalidOperationException("ui"));

    Assert.Single(_notices);
  }

  [Fact]
  public void ABackgroundErrorWithNoObjectStillLogs()
  {
    var policy = NewPolicy();

    policy.LogBackgroundException("unobserved task", null);

    var line = Assert.Single(_log.Errors);
    Assert.Contains("unobserved task", line);
  }
}

public class LauncherUnhandledErrorShellTests : NewProjectFixture
{
  [Fact]
  public void AnUnhandledUiExceptionShowsTheToastWithAnOpenLogsLink()
  {
    var model = NewMain();
    var policy = new UnhandledErrorPolicy(new RecordingLauncherLog(), "C:/data/logs/launcher.log")
    {
      ShowNotice = model.ShowNotice
    };

    policy.HandleUiException(new InvalidOperationException("boom"));

    Assert.Equal(UnhandledErrorPolicy.ToastMessage, model.ToastMessage);
    Assert.True(model.HasToastLog);
    Assert.Equal("C:/data/logs/launcher.log", model.ToastLogPath);
  }
}
