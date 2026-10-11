using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Typed lookup of the current scene's scene scripts from World, object scripts and other scene scripts.
[Collection("BridgeInstances")]
public unsafe class SceneScriptLookupTests : IDisposable
{
  public class MatchState : SceneScript
  {
    public int Score { get; private set; }
    public bool ThrowInFixed { get; set; }

    public void AddPoint() => Score++;

    public int EndRound() => publish("roundEnded", Score);

    public override void fixedUpdate(float dt)
    {
      if (ThrowInFixed)
      {
        throw new InvalidOperationException("boom");
      }
    }
  }

  public class AMatchState : MatchState
  {
  }

  public class BMatchState : MatchState
  {
  }

  public class OtherState : SceneScript
  {
  }

  public class Enemy : ScriptBase
  {
    public int LastSeenScore { get; private set; } = -1;
    public int RoundsEnded { get; private set; }
    public int LastPayload { get; private set; } = -1;

    public MatchState? Lookup() => getSceneScript<MatchState>();

    public MatchState? Require() => requireSceneScript<MatchState>();

    public OtherState? RequireOther() => requireSceneScript<OtherState>();

    public void ReadScore() => LastSeenScore = getSceneScript<MatchState>()?.Score ?? -1;

    public override void start() => subscribe<int>("roundEnded", payload =>
    {
      RoundsEnded++;
      LastPayload = payload;
      LastSeenScore = getSceneScript<MatchState>()?.Score ?? -1;
    });
  }

  public class Helper : SceneScript
  {
    public MatchState? Require() => requireSceneScript<MatchState>();
  }

  private const string SceneUuid = "scene-uuid";
  private const string ObjectUuid = "object-uuid";

  public SceneScriptLookupTests()
  {
    Bridge.ResetForTests();
    Bridge.Events.clear();
  }

  public void Dispose()
  {
    Bridge.ResetForTests();
    Bridge.Events.clear();
  }

  private static readonly List<(int Level, string Message)> LogCalls = new();

  [UnmanagedCallersOnly]
  private static void RecordLog(int level, IntPtr message)
    => LogCalls.Add((level, Marshal.PtrToStringUTF8(message) ?? ""));

  // Points the log binding at a recorder, so nothing depends on the process-wide console streams.
  private static List<string> CaptureErrors(Action action)
  {
    var original = NativeBindings.Log;
    LogCalls.Clear();
    NativeBindings.Log = new LogBindings { write = &RecordLog };
    try
    {
      action();
      return LogCalls.FindAll(call => call.Level == 4).ConvertAll(call => call.Message);
    }
    finally
    {
      NativeBindings.Log = original;
      LogCalls.Clear();
    }
  }

  [Fact]
  public void FindSceneScriptReturnsTheRegisteredInstanceAndNullWhenAbsent()
  {
    Assert.Null(Bridge.FindSceneScript<MatchState>());

    var state = new MatchState();
    Bridge.AddInstance(SceneUuid, nameof(MatchState), state);

    Assert.Same(state, Bridge.FindSceneScript<MatchState>());
    Assert.Null(Bridge.FindSceneScript<OtherState>());
  }

  [Fact]
  public void AFaultedSceneScriptIsExcludedFromLookup()
  {
    var faulty = new MatchState { ThrowInFixed = true };
    var healthy = new OtherState();
    Bridge.AddInstance(SceneUuid, nameof(MatchState), faulty);
    Bridge.AddInstance(SceneUuid, nameof(OtherState), healthy);
    Assert.Same(faulty, Bridge.FindSceneScript<MatchState>());

    Bridge.RunFixedUpdate(SceneUuid, nameof(MatchState), 0.1f);

    Assert.False(Bridge.IsHealthy(SceneUuid, nameof(MatchState)));
    Assert.Null(Bridge.FindSceneScript<MatchState>());
    Assert.Same(healthy, Bridge.FindSceneScript<OtherState>());
  }

  [Fact]
  public void AnExactTypeWinsOverASubclassThatSortsFirst()
  {
    var subclass = new AMatchState();
    Bridge.AddInstance(SceneUuid, nameof(AMatchState), subclass);
    Assert.Same(subclass, Bridge.FindSceneScript<MatchState>());

    var plain = new MatchState();
    Bridge.AddInstance(SceneUuid, nameof(MatchState), plain);

    Assert.Same(plain, Bridge.FindSceneScript<MatchState>());
    Assert.Same(subclass, Bridge.FindSceneScript<AMatchState>());
  }

  [Fact]
  public void WithoutAnExactTypeTheFirstSubclassInKeyOrderWins()
  {
    var second = new BMatchState();
    var first = new AMatchState();
    Bridge.AddInstance(SceneUuid, nameof(BMatchState), second);
    Bridge.AddInstance(SceneUuid, nameof(AMatchState), first);

    Assert.Same(first, Bridge.FindSceneScript<MatchState>());
    Assert.Same(second, Bridge.FindSceneScript<BMatchState>());
  }

  [Fact]
  public void ObjectScriptsDoNotInterfereAndASceneScriptUnderAnObjectUuidIsFound()
  {
    var enemy = new Enemy();
    Bridge.AddInstance(ObjectUuid, nameof(Enemy), enemy);
    Assert.Null(Bridge.FindSceneScript<MatchState>());

    var state = new MatchState();
    Bridge.AddInstance(ObjectUuid, nameof(MatchState), state);

    Assert.Same(state, Bridge.FindSceneScript<MatchState>());
    Assert.Same(enemy, Bridge.FindScript<Enemy>(ObjectUuid));
  }

  [Fact]
  public void WorldTryGetSceneScriptReportsFoundAndMissing()
  {
    Assert.False(World.tryGetSceneScript<MatchState>(out var missing));
    Assert.Null(missing);

    var state = new MatchState();
    Bridge.AddInstance(SceneUuid, nameof(MatchState), state);

    Assert.True(World.tryGetSceneScript<MatchState>(out var found));
    Assert.Same(state, found);
  }

  [Fact]
  public void RequireSceneScriptReturnsTheInstanceWithoutLoggingWhenPresent()
  {
    var state = new MatchState();
    var enemy = new Enemy();
    Bridge.AddInstance(SceneUuid, nameof(MatchState), state);
    Bridge.AddInstance(ObjectUuid, nameof(Enemy), enemy);
    MatchState? result = null;

    var output = CaptureErrors(() => result = enemy.Require());

    Assert.Same(state, result);
    Assert.Empty(output);
    Assert.Same(state, enemy.Lookup());

    Bridge.RemoveInstance(SceneUuid, nameof(MatchState));
    var afterRemoval = CaptureErrors(() => Assert.Null(enemy.Require()));
    Assert.Single(afterRemoval);
  }

  [Fact]
  public void RequireSceneScriptLogsOncePerCallerInstanceAndReturnsNull()
  {
    var first = new Enemy();
    var second = new Enemy();
    Bridge.AddInstance(ObjectUuid, nameof(Enemy), first);
    MatchState? result = null;

    var firstOutput = CaptureErrors(() =>
    {
      result = first.Require();
      first.Require();
      first.Require();
    });
    var secondOutput = CaptureErrors(() => second.Require());
    var quietOutput = CaptureErrors(() => Assert.Null(first.Lookup()));

    Assert.Null(result);
    const string expected = "Script 'Enemy' requires scene script 'MatchState', " +
                            "but the current scene has none (or it has faulted).";
    Assert.Equal(new[] { expected }, firstOutput);
    Assert.Equal(new[] { expected }, secondOutput);
    Assert.Empty(quietOutput);
  }

  [Fact]
  public void ASceneScriptCanRequireAnotherSceneScript()
  {
    var helper = new Helper();
    Bridge.AddInstance(SceneUuid, nameof(Helper), helper);
    var missingOutput = CaptureErrors(() => Assert.Null(helper.Require()));
    Assert.Equal(new[]
    {
      "Script 'Helper' requires scene script 'MatchState', but the current scene has none (or it has faulted)."
    }, missingOutput);

    var state = new MatchState();
    Bridge.AddInstance(SceneUuid, nameof(MatchState), state);

    Assert.Same(state, helper.Require());
  }

  [Fact]
  public void RequireSceneScriptDedupesPerType()
  {
    var enemy = new Enemy();

    var output = CaptureErrors(() =>
    {
      enemy.Require();
      enemy.RequireOther();
      enemy.Require();
      enemy.RequireOther();
    });

    Assert.Equal(2, output.Count);
    Assert.Contains("'MatchState'", output[0]);
    Assert.Contains("'OtherState'", output[1]);
  }

  [Fact]
  public void RequireSceneScriptLogsAgainAfterTheScriptReturnsAndGoesMissingAgain()
  {
    var enemy = new Enemy();
    var state = new MatchState();

    var first = CaptureErrors(() => Assert.Null(enemy.Require()));
    Bridge.AddInstance(SceneUuid, nameof(MatchState), state);
    var whilePresent = CaptureErrors(() => Assert.Same(state, enemy.Require()));
    Bridge.RemoveInstance(SceneUuid, nameof(MatchState));
    var second = CaptureErrors(() => Assert.Null(enemy.Require()));
    var repeated = CaptureErrors(() => Assert.Null(enemy.Require()));

    Assert.Single(first);
    Assert.Empty(whilePresent);
    Assert.Single(second);
    Assert.Empty(repeated);
  }

  [Fact]
  public void AnObjectScriptReadsSceneScriptStateAndReactsToItsEvents()
  {
    var state = new MatchState();
    var enemy = new Enemy();
    Bridge.AddInstance(SceneUuid, nameof(MatchState), state);
    Bridge.AddInstance(ObjectUuid, nameof(Enemy), enemy);
    enemy.start();
    state.AddPoint();
    state.AddPoint();

    enemy.ReadScore();
    Assert.Equal(2, enemy.LastSeenScore);
    Assert.Equal(0, enemy.RoundsEnded);

    state.AddPoint();
    var delivered = state.EndRound();

    Assert.Equal(1, delivered);
    Assert.Equal(1, enemy.RoundsEnded);
    Assert.Equal(3, enemy.LastSeenScore);
    Assert.Equal(3, enemy.LastPayload);
  }
}
