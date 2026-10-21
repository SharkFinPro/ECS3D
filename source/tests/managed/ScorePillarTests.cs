using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Coin Rush's ScorePillar against a stateful Transform fake, driven through Bridge's real event channel the
// way ScoreKeeper's scoreChanged events reach it.
[Collection("BridgeInstances")]
public unsafe class ScorePillarTests : IDisposable
{
  private const string PillarUuid = "pillar";

  private static readonly Dictionary<string, Vector3> Positions = new();
  private static readonly Dictionary<string, Vector3> Scales = new();
  private static readonly List<(int Level, string Message)> LogCalls = new();

  private readonly IDisposable _natives = FakeNatives.Install();
  private readonly LogBindings _originalLog = NativeBindings.Log;

  [UnmanagedCallersOnly]
  private static void RecordLog(int level, IntPtr message)
    => LogCalls.Add((level, Marshal.PtrToStringUTF8(message) ?? ""));

  private static string Id(IntPtr uuid) => Marshal.PtrToStringUTF8(uuid) ?? "";

  [UnmanagedCallersOnly]
  private static void GetLocalPosition(IntPtr u, float* x, float* y, float* z)
  {
    var v = Positions[Id(u)];
    *x = v.X;
    *y = v.Y;
    *z = v.Z;
  }

  [UnmanagedCallersOnly]
  private static void GetLocalScale(IntPtr u, float* x, float* y, float* z)
  {
    var v = Scales[Id(u)];
    *x = v.X;
    *y = v.Y;
    *z = v.Z;
  }

  [UnmanagedCallersOnly]
  private static void SetPosition(IntPtr u, float x, float y, float z) => Positions[Id(u)] = new Vector3(x, y, z);

  [UnmanagedCallersOnly]
  private static void SetScale(IntPtr u, float x, float y, float z) => Scales[Id(u)] = new Vector3(x, y, z);

  public ScorePillarTests()
  {
    Positions.Clear();
    Scales.Clear();
    LogCalls.Clear();
    Bridge.ResetForTests();
    Bridge.Events.clear();

    NativeBindings.Log = new LogBindings { write = &RecordLog };
    NativeBindings.Transform = new TransformBindings
    {
      getLocalPosition = &GetLocalPosition,
      getLocalScale = &GetLocalScale,
      setPosition = &SetPosition,
      setScale = &SetScale
    };
  }

  public void Dispose()
  {
    Bridge.ResetForTests();
    Bridge.Events.clear();
    NativeBindings.Log = _originalLog;
    LogCalls.Clear();
    var error = FakeNatives.LastError;
    _natives.Dispose();
    Assert.Null(error);
  }

  private static ScorePillar StartPillar(Vector3 position, Vector3 scale, params (string Field, object Value)[] fields)
  {
    Positions[PillarUuid] = position;
    Scales[PillarUuid] = scale;

    var pillar = new ScorePillar();
    Bridge.AddInstance(PillarUuid, nameof(ScorePillar), pillar);
    pillar.initComponents();
    foreach (var (field, value) in fields)
    {
      typeof(ScorePillar).GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(pillar, value);
    }

    pillar.start();
    return pillar;
  }

  private static void Score(int slot, int score)
    => Bridge.Events.publish(StarterEvents.ScoreChanged, new ScoreChangedEvent { Slot = slot, Score = score });

  private static float Height() => Scales[PillarUuid].Y * 2.0f;

  private static float Base() => Positions[PillarUuid].Y - Scales[PillarUuid].Y;

  private static readonly Vector3 Ground = new(-28.0f, 0.1f, -24.0f);
  private static readonly Vector3 Slim = new(0.75f, 0.1f, 0.75f);

  [Fact]
  public void Height_AddsUnitsPerPointToTheMinimumAndClampsToTheMaximum()
  {
    Assert.Equal(0.2f, ScorePillarMath.height(0, 0.5f, 0.2f, 12.0f), 4);
    Assert.Equal(2.2f, ScorePillarMath.height(4, 0.5f, 0.2f, 12.0f), 4);
    Assert.Equal(12.0f, ScorePillarMath.height(100, 0.5f, 0.2f, 12.0f), 4);
    Assert.Equal(0.2f, ScorePillarMath.height(-3, 0.5f, 0.2f, 12.0f), 4);
  }

  [Fact]
  public void Height_ARangeThatInvertsOrIsNotFiniteStaysUsable()
  {
    Assert.Equal(3.0f, ScorePillarMath.height(50, 1.0f, 3.0f, 1.0f), 4);
    Assert.Equal(0.0f, ScorePillarMath.height(5, float.NaN, float.NaN, 12.0f), 4);
    Assert.Equal(5.0f, ScorePillarMath.height(5, 1.0f, 0.0f, float.NaN), 4);
  }

  [Fact]
  public void CenterY_SitsHalfTheHeightAboveTheBase()
  {
    Assert.Equal(2.5f, ScorePillarMath.centerY(1.0f, 3.0f), 4);
    Assert.Equal(1.5f, ScorePillarMath.halfHeight(3.0f), 4);
  }

  [Fact]
  public void ScoreForItsSlot_SetsTheHeightAndLeavesTheFootprint()
  {
    StartPillar(Ground, Slim);

    Score(0, 4);

    Assert.Equal(2.2f, Height(), 3);
    Assert.Equal(Slim.X, Scales[PillarUuid].X, 4);
    Assert.Equal(Slim.Z, Scales[PillarUuid].Z, 4);
    Assert.Equal(Ground.X, Positions[PillarUuid].X, 4);
    Assert.Equal(Ground.Z, Positions[PillarUuid].Z, 4);
  }

  [Fact]
  public void ScoreForAnotherSlot_IsIgnoredWhileItsOwnSlotStillWorks()
  {
    StartPillar(Ground, Slim, ("m_slot", 2));

    Score(0, 9);
    Score(1, 9);
    Assert.Equal(0.2f, Height(), 3);

    Score(2, 3);
    Assert.Equal(1.7f, Height(), 3);
  }

  [Fact]
  public void TheBaseStaysWhereItWasPlacedAsThePillarGrows()
  {
    StartPillar(new Vector3(28.0f, 0.5f + 0.1f, 24.0f), Slim);

    foreach (var score in new[] { 1, 7, 3, 20 })
    {
      Score(0, score);
      Assert.Equal(0.5f, Base(), 3);
    }
  }

  [Fact]
  public void AScoreOfZeroReturnsThePillarToItsMinimumHeight()
  {
    StartPillar(Ground, Slim);

    Score(0, 6);
    Assert.Equal(3.2f, Height(), 3);

    Score(0, 0);

    Assert.Equal(0.2f, Height(), 3);
    Assert.Equal(0.0f, Base(), 3);
  }

  [Fact]
  public void StartingTakesTheMinimumHeightFromAnyAuthoredSize()
  {
    StartPillar(new Vector3(0.0f, 3.0f, 0.0f), new Vector3(1.0f, 3.0f, 1.0f), ("m_minHeight", 1.0f));

    Assert.Equal(1.0f, Height(), 3);
    Assert.Equal(0.0f, Base(), 3);
  }

  [Fact]
  public void TheHeightStopsAtMaxHeight()
  {
    StartPillar(Ground, Slim, ("m_maxHeight", 4.0f));

    Score(0, 1000);

    Assert.Equal(4.0f, Height(), 3);
    Assert.Equal(0.0f, Base(), 3);
  }

  [Fact]
  public void ARoundResetReachesThePillarThroughScoreKeeper()
  {
    var keeper = new ScoreKeeper();
    Bridge.AddInstance("scene", nameof(ScoreKeeper), keeper);
    keeper.start();
    StartPillar(Ground, Slim);

    Bridge.Events.publish(StarterEvents.Collected, new CollectedEvent { Slot = 0, Value = 5 });
    Assert.Equal(2.7f, Height(), 3);

    Bridge.Events.publish(StarterEvents.RoundOver, new RoundOverEvent { Round = 1 });

    Assert.Equal(0.2f, Height(), 3);
    Assert.Empty(LogCalls.Where(c => c.Level == 3));
  }
}
