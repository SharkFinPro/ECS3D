using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using ECS3DLauncher.Services;

namespace ECS3DManagedTests;

// Keeps every process it starts, which FakeProcessRunner (last one only) does not.
internal sealed class RecordingProcessRunner : IProcessRunner
{
  public List<ProcessLaunch> Launches { get; } = [];

  public List<FakeRunningProcess> Processes { get; } = [];

  public Func<ProcessLaunch, string?>? FailFor { get; set; }

  public IRunningProcess Start(ProcessLaunch launch)
  {
    var failure = FailFor?.Invoke(launch);
    if (failure is not null)
    {
      throw new ProcessStartException(failure);
    }

    var process = new FakeRunningProcess();
    Launches.Add(launch);
    Processes.Add(process);
    return process;
  }
}

internal sealed class FakePortProbe : IPortProbe
{
  public bool Accepts { get; set; }

  public int Calls { get; private set; }

  public Task<bool> AcceptsConnectionAsync(string host, int port, CancellationToken cancellationToken)
  {
    cancellationToken.ThrowIfCancellationRequested();
    Calls++;
    return Task.FromResult(Accepts);
  }
}

internal sealed class FakeClock : TimeProvider
{
  private long _ticks;

  public DateTimeOffset Now { get; set; } = new(2026, 1, 1, 12, 0, 0, TimeSpan.Zero);

  public override long TimestampFrequency => TimeSpan.TicksPerSecond;

  public override long GetTimestamp() => _ticks;

  public override DateTimeOffset GetUtcNow() => Now;

  public void Advance(TimeSpan span)
  {
    _ticks += span.Ticks;
    Now += span;
  }
}
