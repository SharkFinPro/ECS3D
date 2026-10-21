using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// ScoreKeeper and RoundTimer driven through Bridge's real event channel and scheduler. Time advances with
// RunFixedUpdate ticks; info and warning log lines come through a recording Log binding.
[Collection("BridgeInstances")]
public unsafe class StarterScoringTests : IDisposable
{
  public class Recorder : SceneScript
  {
    public List<ScoreChangedEvent> Scores { get; } = new();
    public List<MatchWonEvent> Wins { get; } = new();
    public List<int> Ticks { get; } = new();
    public List<int> Overs { get; } = new();
    public List<int> Starts { get; } = new();

    public override void start()
    {
      subscribe<ScoreChangedEvent>(StarterEvents.ScoreChanged, Scores.Add);
      subscribe<MatchWonEvent>(StarterEvents.MatchWon, Wins.Add);
      subscribe<RoundTickEvent>(StarterEvents.RoundTick, e => Ticks.Add(e.SecondsLeft));
      subscribe<RoundOverEvent>(StarterEvents.RoundOver, e => Overs.Add(e.Round));
      subscribe<RoundStartedEvent>(StarterEvents.RoundStarted, e => Starts.Add(e.Round));
    }
  }

  public class Orderer : SceneScript
  {
    public List<string> Order { get; } = new();

    public override void start()
    {
      subscribe<RoundTickEvent>(StarterEvents.RoundTick, e => Order.Add("tick" + e.SecondsLeft));
      subscribe<RoundOverEvent>(StarterEvents.RoundOver, e => Order.Add("over" + e.Round));
    }
  }

  private const string SceneUuid = "scene-uuid";
  private const string Keeper = nameof(ScoreKeeper);
  private const string Timer = nameof(RoundTimer);

  private static readonly List<(int Level, string Message)> LogCalls = new();
  private readonly LogBindings _originalLog = NativeBindings.Log;

  [UnmanagedCallersOnly]
  private static void RecordLog(int level, IntPtr message)
    => LogCalls.Add((level, Marshal.PtrToStringUTF8(message) ?? ""));

  public StarterScoringTests()
  {
    Bridge.ResetForTests();
    Bridge.Events.clear();
    LogCalls.Clear();
    NativeBindings.Log = new LogBindings { write = &RecordLog };
  }

  public void Dispose()
  {
    NativeBindings.Log = _originalLog;
    LogCalls.Clear();
    Bridge.ResetForTests();
    Bridge.Events.clear();
  }

  private static void SetField(object target, string name, object value)
  {
    var field = target.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic);
    Assert.NotNull(field);
    field!.SetValue(target, value);
  }

  private static Recorder AddRecorder()
  {
    var recorder = new Recorder();
    Bridge.AddInstance(SceneUuid, nameof(Recorder), recorder);
    recorder.start();
    return recorder;
  }

  private static ScoreKeeper AddKeeper(int target = 0, bool reset = true)
  {
    var keeper = new ScoreKeeper();
    SetField(keeper, "m_targetScore", target);
    SetField(keeper, "m_resetOnRoundOver", reset);
    Bridge.AddInstance(SceneUuid, Keeper, keeper);
    keeper.start();
    return keeper;
  }

  private static RoundTimer AddTimer(int seconds, float intermission = 2f, bool restart = true, bool startOnLoad = true)
  {
    var timer = new RoundTimer();
    SetField(timer, "m_roundSeconds", seconds);
    SetField(timer, "m_intermissionSeconds", intermission);
    SetField(timer, "m_autoRestart", restart);
    SetField(timer, "m_startOnLoad", startOnLoad);
    Bridge.AddInstance(SceneUuid, Timer, timer);
    timer.start();
    return timer;
  }

  private static void Collect(int slot, int value)
    => Bridge.Events.publish(StarterEvents.Collected, new CollectedEvent { Slot = slot, Value = value });

  private static void RoundOver(int round)
    => Bridge.Events.publish(StarterEvents.RoundOver, new RoundOverEvent { Round = round });

  private static void Run(float seconds)
  {
    for (var tick = 0; tick < (int)(seconds * 2); tick++)
    {
      Bridge.RunFixedUpdate(SceneUuid, Timer, 0.5f);
    }
  }

  private static string[] Infos() => LogCalls.Where(c => c.Level == 2).Select(c => c.Message).ToArray();

  private static string[] Warnings() => LogCalls.Where(c => c.Level == 3).Select(c => c.Message).ToArray();

  [Fact]
  public void ScoreKeeper_TotalsEachSlotSeparately()
  {
    var keeper = AddKeeper();

    Collect(0, 1);
    Collect(1, 2);
    Collect(0, 3);

    Assert.Equal(4, keeper.getScore(0));
    Assert.Equal(2, keeper.getScore(1));
    Assert.Equal(0, keeper.getScore(5));
  }

  [Fact]
  public void ScoreKeeper_LogsTheLeaderboardWithSlotsAscending()
  {
    AddKeeper();

    Collect(1, 2);
    Collect(0, 4);

    Assert.Equal(new[] { "Scores: P1 2", "Scores: P0 4, P1 2" }, Infos());
  }

  [Fact]
  public void ScoreKeeper_PublishesTheNewTotalOnEveryChange()
  {
    var recorder = AddRecorder();
    AddKeeper();

    Collect(0, 1);
    Collect(0, 3);
    Collect(1, 2);

    Assert.Equal(new[] { (0, 1), (0, 4), (1, 2) }, recorder.Scores.Select(e => (e.Slot, e.Score)).ToArray());
  }

  [Fact]
  public void ScoreKeeper_ListensOnTheConfiguredEventName()
  {
    var keeper = new ScoreKeeper();
    SetField(keeper, "m_eventName", "gem");
    Bridge.AddInstance(SceneUuid, Keeper, keeper);
    keeper.start();

    Collect(0, 1);
    Assert.Equal(0, keeper.getScore(0));

    Bridge.Events.publish("gem", new CollectedEvent { Slot = 0, Value = 5 });
    Assert.Equal(5, keeper.getScore(0));
  }

  [Fact]
  public void ScoreKeeper_IgnoresACollectionByNoPlayer()
  {
    var recorder = AddRecorder();
    var keeper = AddKeeper();

    Collect(-1, 5);
    Assert.Empty(recorder.Scores);

    Collect(0, 5);
    Assert.Equal(5, keeper.getScore(0));
    Assert.Single(recorder.Scores);
  }

  [Fact]
  public void ScoreKeeper_PublishesTheMatchWinOnceAtTheTarget()
  {
    var recorder = AddRecorder();
    AddKeeper(target: 5);

    Collect(0, 2);
    Collect(1, 4);
    Assert.Empty(recorder.Wins);

    Collect(1, 1);
    Collect(1, 1);
    Collect(0, 9);

    var win = Assert.Single(recorder.Wins);
    Assert.Equal(1, win.Slot);
    Assert.Equal(5, win.Score);
    Assert.Single(Infos(), line => line.Contains("wins the match"));
    Assert.Contains("P1 wins the match with 5", Infos());
  }

  [Fact]
  public void ScoreKeeper_NeverWinsWithoutATarget()
  {
    var recorder = AddRecorder();
    AddKeeper();

    Collect(0, 1000);

    Assert.Empty(recorder.Wins);
    Assert.Single(recorder.Scores);
  }

  [Fact]
  public void ScoreKeeper_RoundOverNamesTheHighestScorerAndResetsEveryScore()
  {
    var recorder = AddRecorder();
    var keeper = AddKeeper();
    Collect(0, 4);
    Collect(1, 2);
    recorder.Scores.Clear();
    LogCalls.Clear();

    RoundOver(3);

    Assert.Equal(new[] { "Round 3 over: P0 wins with 4", "Scores: P0 0, P1 0" }, Infos());
    Assert.Equal(new[] { (0, 0), (1, 0) }, recorder.Scores.Select(e => (e.Slot, e.Score)).ToArray());
    Assert.Equal(0, keeper.getScore(0));
    Assert.Equal(0, keeper.getScore(1));
  }

  [Fact]
  public void ScoreKeeper_RoundOverWithEqualTopScoresIsATie()
  {
    AddKeeper();
    Collect(0, 3);
    Collect(1, 3);
    Collect(2, 1);
    LogCalls.Clear();

    RoundOver(1);

    Assert.Equal(new[] { "Round 1 over: tie at 3", "Scores: P0 0, P1 0, P2 0" }, Infos());
  }

  [Fact]
  public void ScoreKeeper_RoundOverWithNoScoresSaysSo()
  {
    var recorder = AddRecorder();
    AddKeeper();

    RoundOver(1);

    Assert.Equal(new[] { "Round 1 over: no scores" }, Infos());
    Assert.Empty(recorder.Scores);
  }

  [Fact]
  public void ScoreKeeper_RoundOverWithoutResetKeepsScoresAndPublishesNothing()
  {
    var recorder = AddRecorder();
    var keeper = AddKeeper(reset: false);
    Collect(0, 4);
    recorder.Scores.Clear();

    RoundOver(1);

    Assert.Equal(4, keeper.getScore(0));
    Assert.Empty(recorder.Scores);
    Assert.Contains("Round 1 over: P0 wins with 4", Infos());
  }

  [Fact]
  public void ScoreKeeper_ResetRearmsTheMatchWin()
  {
    var recorder = AddRecorder();
    AddKeeper(target: 3);
    Collect(0, 3);
    Assert.Single(recorder.Wins);

    RoundOver(1);
    Collect(1, 3);

    Assert.Equal(new[] { 0, 1 }, recorder.Wins.Select(e => e.Slot).ToArray());
  }

  [Fact]
  public void ScoreKeeper_WithoutResetTheMatchWinStaysLatched()
  {
    var recorder = AddRecorder();
    AddKeeper(target: 3, reset: false);
    Collect(0, 3);

    RoundOver(1);
    Collect(1, 3);

    Assert.Single(recorder.Wins);
  }

  [Fact]
  public void ScoreKeeper_DropsASlotWhenItsPlayerLeaves()
  {
    var keeper = AddKeeper();
    Collect(0, 4);
    Collect(1, 2);

    keeper.onPlayerLeft(0);

    Assert.Equal(0, keeper.getScore(0));
    Assert.Equal(2, keeper.getScore(1));
    LogCalls.Clear();
    Collect(1, 1);
    Assert.Equal(new[] { "Scores: P1 3" }, Infos());
  }

  [Fact]
  public void ScoreKeeper_PublishesZeroForASlotThatLeavesWithAScore()
  {
    var recorder = AddRecorder();
    var keeper = AddKeeper();
    Collect(0, 4);
    Assert.Equal(4, keeper.getScore(0));
    recorder.Scores.Clear();

    keeper.onPlayerLeft(0);

    var change = Assert.Single(recorder.Scores);
    Assert.Equal((0, 0), (change.Slot, change.Score));
  }

  [Fact]
  public void ScoreKeeper_PublishesNothingWhenASlotWithoutAScoreLeaves()
  {
    var recorder = AddRecorder();
    var keeper = AddKeeper();
    Collect(1, 2);
    recorder.Scores.Clear();

    keeper.onPlayerLeft(0);
    Assert.Empty(recorder.Scores);

    keeper.onPlayerLeft(1);
    Assert.Single(recorder.Scores);
  }

  [Fact]
  public void RoundTimer_PublishesOneTickPerSecondCountingDown()
  {
    var recorder = AddRecorder();
    AddTimer(3);

    Run(0.5f);
    Assert.Empty(recorder.Ticks);

    Run(2.5f);

    Assert.Equal(new[] { 2, 1, 0 }, recorder.Ticks);
  }

  [Fact]
  public void RoundTimer_PublishesRoundOverWithTheRoundNumberAtZero()
  {
    var recorder = AddRecorder();
    AddTimer(3);

    Run(2.5f);
    Assert.Empty(recorder.Overs);

    Run(0.5f);
    Assert.Equal(new[] { 1 }, recorder.Overs);
  }

  [Fact]
  public void RoundTimer_StartsTheNextRoundAfterTheIntermission()
  {
    var recorder = AddRecorder();
    AddTimer(3);

    Run(4.5f);
    Assert.Empty(recorder.Starts);

    Run(0.5f);
    Assert.Equal(new[] { 2 }, recorder.Starts);

    Run(3f);
    Assert.Equal(new[] { 2, 1, 0, 2, 1, 0 }, recorder.Ticks);
    Assert.Equal(new[] { 1, 2 }, recorder.Overs);
  }

  [Fact]
  public void RoundTimer_DoesNotRestartWhenAutoRestartIsOff()
  {
    var recorder = AddRecorder();
    AddTimer(3, restart: false);

    Run(3f);
    Assert.Equal(new[] { 1 }, recorder.Overs);

    Run(20f);

    Assert.Empty(recorder.Starts);
    Assert.Equal(new[] { 2, 1, 0 }, recorder.Ticks);
  }

  [Fact]
  public void RoundTimer_LogsAtTheMarkedSeconds()
  {
    AddTimer(12);

    Run(12f);

    var left = Infos().Where(line => line.Contains("s left")).ToArray();
    Assert.Equal(
      new[]
      {
        "Round 1: 10s left", "Round 1: 5s left", "Round 1: 4s left", "Round 1: 3s left", "Round 1: 2s left",
        "Round 1: 1s left"
      },
      left);
    Assert.Contains("Round 1 started: 12s", Infos());
    Assert.Contains("Round 1 over", Infos());
  }

  [Fact]
  public void RoundTimer_LogsAtSixtyAndThirtySeconds()
  {
    AddTimer(61);

    Run(1f);
    Assert.Contains("Round 1: 60s left", Infos());
    Assert.DoesNotContain("Round 1: 30s left", Infos());

    Run(30f);
    Assert.Contains("Round 1: 30s left", Infos());
    Assert.DoesNotContain("Round 1: 59s left", Infos());
  }

  [Fact]
  public void RoundTimer_WarnsOnceAndDoesNothingForAnEmptyRound()
  {
    var recorder = AddRecorder();
    var timer = AddTimer(0);

    Run(5f);
    timer.beginRound();
    Run(5f);

    Assert.Empty(recorder.Ticks);
    Assert.Empty(recorder.Overs);
    Assert.Single(Warnings());
    Assert.Contains("Round Seconds", Warnings()[0]);
  }

  [Fact]
  public void RoundTimer_StaysIdleWhenStartOnLoadIsOffUntilBeginRound()
  {
    var recorder = AddRecorder();
    var timer = AddTimer(3, startOnLoad: false);

    Run(5f);
    Assert.Empty(recorder.Ticks);

    timer.beginRound();
    Run(3f);

    Assert.Equal(new[] { 2, 1, 0 }, recorder.Ticks);
  }

  [Fact]
  public void RoundTimer_BeginRoundDuringARoundDoesNotStartASecondCountdown()
  {
    var recorder = AddRecorder();
    var timer = AddTimer(3);

    Run(1f);
    timer.beginRound();
    Run(2f);

    Assert.Equal(new[] { 2, 1, 0 }, recorder.Ticks);
  }

  [Fact]
  public void ScoreKeeper_ANegativeTopScoreStillNamesAWinner()
  {
    AddKeeper();
    Collect(0, -3);
    Collect(1, -1);
    LogCalls.Clear();

    RoundOver(1);

    Assert.Contains("Round 1 over: P1 wins with -1", Infos());
  }

  [Fact]
  public void RoundTimer_PublishesNoRoundStartedWhenTheRoundCannotStart()
  {
    var recorder = AddRecorder();
    var timer = AddTimer(3);
    Run(3f);
    Assert.Equal(new[] { 1 }, recorder.Overs);

    SetField(timer, "m_roundSeconds", 0);
    Run(3f);

    Assert.Empty(recorder.Starts);
    Assert.Equal(new[] { 2, 1, 0 }, recorder.Ticks);
    Assert.Single(Warnings());
  }

  [Fact]
  public void RoundTimer_PublishesTheLastTickBeforeRoundOver()
  {
    var orderer = new Orderer();
    Bridge.AddInstance(SceneUuid, nameof(Orderer), orderer);
    orderer.start();
    AddTimer(2, restart: false);

    Run(2f);

    Assert.Equal(new[] { "tick1", "tick0", "over1" }, orderer.Order);
  }

  [Fact]
  public void ScoreKeeper_StopsScoringOnceRemoved()
  {
    var keeper = AddKeeper();
    Collect(0, 2);

    Bridge.RemoveInstance(SceneUuid, Keeper);
    Collect(0, 5);

    Assert.Equal(2, keeper.getScore(0));
  }

  [Fact]
  public void RoundTimer_StopsTickingOnceRemoved()
  {
    var recorder = AddRecorder();
    AddTimer(5);
    Run(1f);
    Assert.Equal(new[] { 4 }, recorder.Ticks);

    Bridge.RemoveInstance(SceneUuid, Timer);
    Run(3f);

    Assert.Equal(new[] { 4 }, recorder.Ticks);
  }

  [Fact]
  public void ScoreKeeper_IsReachableThroughTheWorldLookup()
  {
    var keeper = AddKeeper();
    Collect(1, 7);

    Assert.True(World.tryGetSceneScript<ScoreKeeper>(out var found));

    Assert.Same(keeper, found);
    Assert.Equal(7, found.getScore(1));
  }
}
