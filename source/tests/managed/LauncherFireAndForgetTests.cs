using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

[CollectionDefinition("FireAndForget", DisableParallelization = true)]
public sealed class FireAndForgetCollection;

public class LauncherFireAndForgetTests
{
  private static readonly TimeSpan Wait = TimeSpan.FromSeconds(10);

  private readonly List<Exception> _reports = [];
  private readonly TaskCompletionSource _reported = new(TaskCreationOptions.RunContinuationsAsynchronously);

  private void Report(Exception error)
  {
    _reports.Add(error);
    _reported.TrySetResult();
  }

  // Forgetting a task that completes later runs its observer after every earlier one, so a fault forgotten
  // before this sentinel has already been reported when the sentinel's report lands.
  private async Task SentinelAsync()
  {
    var sentinel = new TaskCompletionSource();
    var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    sentinel.Task.Forget(_ => done.SetResult());
    sentinel.SetException(new InvalidOperationException("sentinel"));
    await done.Task.WaitAsync(Wait);
  }

  [Fact]
  public async Task AFaultedTaskReportsItsExceptionOnce()
  {
    var original = new InvalidOperationException("boom");

    Task.FromException(original).Forget(Report);
    await _reported.Task.WaitAsync(Wait);

    Assert.Same(original, Assert.Single(_reports));
  }

  [Fact]
  public async Task ATaskThatFaultsLaterReportsWhenItFails()
  {
    var source = new TaskCompletionSource();
    source.Task.Forget(Report);
    Assert.Empty(_reports);

    var original = new InvalidOperationException("late");
    source.SetException(original);
    await _reported.Task.WaitAsync(Wait);

    Assert.Same(original, Assert.Single(_reports));
  }

  [Fact]
  public async Task ACanceledTaskReportsNothing()
  {
    Task.FromCanceled(new System.Threading.CancellationToken(true)).Forget(Report);

    await SentinelAsync();

    Assert.Empty(_reports);
  }

  [Fact]
  public async Task ASuccessfulTaskReportsNothing()
  {
    Task.CompletedTask.Forget(Report);
    var source = new TaskCompletionSource();
    source.Task.Forget(Report);
    source.SetResult();

    await SentinelAsync();

    Assert.Empty(_reports);
  }

  [Fact]
  public async Task AnAsyncMethodThatThrowsReportsTheInnerException()
  {
    static async Task FailsAfterYield()
    {
      await Task.Yield();
      throw new InvalidOperationException("async failure");
    }

    FailsAfterYield().Forget(Report);
    await _reported.Task.WaitAsync(Wait);

    var error = Assert.Single(_reports);
    Assert.IsType<InvalidOperationException>(error);
    Assert.Equal("async failure", error.Message);
  }

  [Fact]
  public async Task SeveralFaultsAreReportedTogether()
  {
    var first = new InvalidOperationException("one");
    var second = new InvalidOperationException("two");

    Task.WhenAll(Task.FromException(first), Task.FromException(second)).Forget(Report);
    await _reported.Task.WaitAsync(Wait);

    var aggregate = Assert.IsType<AggregateException>(Assert.Single(_reports));
    Assert.Equal([first, second], aggregate.InnerExceptions);
  }

  [Fact]
  public async Task AReporterThatThrowsDoesNotEscape()
  {
    Task.FromException(new InvalidOperationException("boom")).Forget(_ => throw new InvalidOperationException("report"));

    await SentinelAsync();
  }
}

[Collection("FireAndForget")]
public class LauncherFireAndForgetInstallTests : NewProjectFixture, IDisposable
{
  private readonly List<Exception> _reports = [];
  private readonly TaskCompletionSource _reported = new(TaskCreationOptions.RunContinuationsAsynchronously);

  public LauncherFireAndForgetInstallTests()
  {
    FireAndForget.Install(error =>
    {
      _reports.Add(error);
      _reported.TrySetResult();
    });
  }

  public void Dispose() => FireAndForget.Install(null);

  [Fact]
  public async Task TheInstalledReporterReceivesAForgottenFault()
  {
    var original = new InvalidOperationException("installed");

    Task.FromException(original).Forget();
    await _reported.Task.WaitAsync(TimeSpan.FromSeconds(10));

    Assert.Same(original, Assert.Single(_reports));
  }

  [Fact]
  public void WithoutAReporterAForgottenFaultIsDroppedQuietly()
  {
    FireAndForget.Install(null);

    Task.FromException(new InvalidOperationException("nobody listening")).Forget();
  }

  [Fact]
  public async Task ACommandThatFaultsBeforeItsOwnHandlingReachesTheReporter()
  {
    _files.UnreadableUnder = "/new";
    var model = NewMain();
    model.PickProjectFile = () => Task.FromResult<string?>("/new/proj");

    model.OpenExistingCommand.Execute(null);
    await _reported.Task.WaitAsync(TimeSpan.FromSeconds(10));

    Assert.IsType<UnauthorizedAccessException>(Assert.Single(_reports));
  }
}

public class LauncherTaskExceptionPolicyTests
{
  private const string LogPath = "logs/launcher.log";

  private readonly RecordingLauncherLog _log = new();
  private readonly List<LaunchNotice> _notices = [];

  private UnhandledErrorPolicy NewPolicy(Func<DateTimeOffset>? now = null) =>
    new(_log, LogPath, now ?? (() => DateTimeOffset.UnixEpoch)) { ShowNotice = _notices.Add };

  [Fact]
  public void ATaskExceptionIsLoggedAsACommandTaskAndRequestsTheToast()
  {
    var handled = NewPolicy().HandleTaskException(new InvalidOperationException("boom"));

    Assert.True(handled);
    var line = Assert.Single(_log.Errors);
    Assert.StartsWith("Unhandled exception in a command task:", line);
    Assert.Contains("boom", line);
    var notice = Assert.Single(_notices);
    Assert.Equal(UnhandledErrorPolicy.ToastMessage, notice.Message);
    Assert.Equal(LogPath, notice.LogPath);
  }

  [Fact]
  public void TaskExceptionsPastTheStormLimitAreNotHandled()
  {
    var policy = NewPolicy();

    for (var i = 0; i < UnhandledErrorPolicy.StormLimit; i++)
    {
      Assert.True(policy.HandleTaskException(new InvalidOperationException("spin")));
    }

    Assert.False(policy.HandleTaskException(new InvalidOperationException("spin")));
    Assert.Equal(UnhandledErrorPolicy.StormLimit, _notices.Count);
  }

  [Fact]
  public void AnOutOfMemoryTaskExceptionIsLoggedButNotHandled()
  {
    var handled = NewPolicy().HandleTaskException(new OutOfMemoryException("no memory"));

    Assert.False(handled);
    Assert.Contains("no memory", Assert.Single(_log.Errors));
    Assert.Empty(_notices);
  }
}
