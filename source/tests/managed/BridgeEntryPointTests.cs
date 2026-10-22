using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text.Json;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Calls Bridge's [UnmanagedCallersOnly] entry points through function pointers, the way the native side
// does, so the UTF-8 argument marshalling and each entry point's routing into the fault gate run here too.
// Shares the BridgeInstances collection with the other classes that touch Bridge's static state.
[Collection("BridgeInstances")]
public sealed unsafe class BridgeEntryPointTests : IDisposable
{
  private class Counter : ScriptBase
  {
    public int Starts;
    public int Stops;
    public int Variables;
    public int Fixeds;
    public float LastDt;
    public bool ThrowInFixed;
    public readonly List<string> Collisions = new();

    public override void start() => Starts++;

    public override void stop() => Stops++;

    public override void variableUpdate() => Variables++;

    public override void fixedUpdate(float dt)
    {
      Fixeds++;
      LastDt = dt;
      if (ThrowInFixed)
      {
        throw new InvalidOperationException("fixed failed");
      }
    }

    public override void onCollisionEnter(string otherUuid) => Collisions.Add("enter:" + otherUuid);

    public override void onCollisionStay(string otherUuid) => Collisions.Add("stay:" + otherUuid);

    public override void onCollisionExit(string otherUuid) => Collisions.Add("exit:" + otherUuid);
  }

  private class SceneRecorder : SceneScript
  {
    public readonly List<string> Events = new();

    public override void onSceneLoaded() => Events.Add("loaded");

    public override void onObjectSpawned(string uuid) => Events.Add("spawned:" + uuid);

    public override void onPlayerJoined(int slot) => Events.Add("joined:" + slot);
  }

  private class Exposed : ScriptBase
  {
    [ExposeToEditor] public float Speed;
    [ExposeToEditor("Count")] public int Number;
    [ExposeToEditor] public bool Flag;
    [ExposeToEditor] public string Label = "";
    [ExposeToEditor] public Vector3 Offset;
    public int Hidden = 5;
  }

  private const string Uuid = "entry-uuid";
  private const string Other = "entry-other";

  private readonly string _dir = Path.Combine(Path.GetTempPath(), "ecs3d-entry-" + Guid.NewGuid().ToString("N"));

  public BridgeEntryPointTests()
  {
    Bridge.ResetForTests();
  }

  public void Dispose()
  {
    try
    {
      Bridge.ResetForTests();
    }
    finally
    {
      if (Directory.Exists(_dir))
      {
        Directory.Delete(_dir, recursive: true);
      }
    }
  }

  private readonly ref struct NativeUtf8
  {
    public readonly IntPtr Ptr;

    public NativeUtf8(string value) => Ptr = Marshal.StringToCoTaskMemUTF8(value);

    public void Dispose() => Marshal.FreeCoTaskMem(Ptr);
  }

  private static T Seed<T>(string className, string uuid = Uuid) where T : ScriptInstance, new()
  {
    var instance = new T();
    Bridge.AddInstance(uuid, className, instance);
    return instance;
  }

  private static byte Healthy(string className, string uuid = Uuid)
  {
    delegate* unmanaged<IntPtr, IntPtr, byte> isScriptHealthy = &Bridge.isScriptHealthy;
    using var u = new NativeUtf8(uuid);
    using var c = new NativeUtf8(className);
    return isScriptHealthy(u.Ptr, c.Ptr);
  }

  private static void Call(delegate* unmanaged<IntPtr, IntPtr, void> entry, string className, string uuid = Uuid)
  {
    using var u = new NativeUtf8(uuid);
    using var c = new NativeUtf8(className);
    entry(u.Ptr, c.Ptr);
  }

  private static void CallFixedUpdate(string className, float dt, string uuid = Uuid)
  {
    delegate* unmanaged<IntPtr, IntPtr, float, void> fixedUpdate = &Bridge.fixedUpdate;
    using var u = new NativeUtf8(uuid);
    using var c = new NativeUtf8(className);
    fixedUpdate(u.Ptr, c.Ptr, dt);
  }

  private static string TakeString(IntPtr ptr)
  {
    delegate* unmanaged<IntPtr, void> freeString = &Bridge.freeString;
    try
    {
      return Marshal.PtrToStringUTF8(ptr)!;
    }
    finally
    {
      freeString(ptr);
    }
  }

  private void CompileScripts()
  {
    Directory.CreateDirectory(_dir);
    File.WriteAllText(Path.Combine(_dir, "scripts.cs"),
      "using ScriptBridge;\npublic class EntryObject : ScriptBase { }\npublic class EntryScene : SceneScript { }\n");
    Bridge.Initialize(_dir);
  }

  [Fact]
  public void AttachScript_CreatesAnInstanceForACompiledClassAndRefusesAnUnknownOne()
  {
    CompileScripts();
    delegate* unmanaged<IntPtr, IntPtr, byte> attach = &Bridge.attachScript;
    using var u = new NativeUtf8(Uuid);
    using var known = new NativeUtf8("EntryObject");
    using var unknown = new NativeUtf8("NoSuchClass");

    Assert.Equal(0, Healthy("EntryObject"));
    Assert.Equal(1, attach(u.Ptr, known.Ptr));
    Assert.Equal(1, Healthy("EntryObject"));
    Assert.Equal(0, attach(u.Ptr, unknown.Ptr));
    Assert.Equal(0, Healthy("NoSuchClass"));
  }

  [Fact]
  public void AttachSceneScript_AcceptsOnlyASceneScriptClass()
  {
    CompileScripts();
    delegate* unmanaged<IntPtr, IntPtr, byte> attachScene = &Bridge.attachSceneScript;
    using var u = new NativeUtf8("scene-uuid");
    using var scene = new NativeUtf8("EntryScene");
    using var objectClass = new NativeUtf8("EntryObject");

    Assert.Equal(1, attachScene(u.Ptr, scene.Ptr));
    Assert.Equal(1, Healthy("EntryScene", "scene-uuid"));
    Assert.Equal(0, attachScene(u.Ptr, objectClass.Ptr));
    Assert.Equal(0, Healthy("EntryObject", "scene-uuid"));
  }

  [Fact]
  public void DetachScript_RemovesTheInstanceAndClearsItsFault()
  {
    var counter = Seed<Counter>("Counter");
    counter.ThrowInFixed = true;
    CallFixedUpdate("Counter", 0.1f);
    Assert.Equal(0, Healthy("Counter"));

    Call(&Bridge.detachScript, "Counter");
    Seed<Counter>("Counter");

    Assert.Equal(1, Healthy("Counter"));
    Call(&Bridge.detachScript, "Counter");
    Assert.Equal(0, Healthy("Counter"));
  }

  [Fact]
  public void StartVariableUpdateAndStop_RouteToTheInstanceUnderTheKey()
  {
    var target = Seed<Counter>("Counter");
    var sibling = Seed<Counter>("Counter", Other);

    Call(&Bridge.start, "Counter");
    Call(&Bridge.variableUpdate, "Counter");
    Call(&Bridge.variableUpdate, "Counter");
    Call(&Bridge.stop, "Counter");

    Assert.Equal((1, 2, 1), (target.Starts, target.Variables, target.Stops));
    Assert.Equal((0, 0, 0), (sibling.Starts, sibling.Variables, sibling.Stops));
  }

  [Fact]
  public void FixedUpdate_PassesDtToTheInstanceUnderTheKey()
  {
    var target = Seed<Counter>("Counter");
    var sibling = Seed<Counter>("Counter", Other);

    CallFixedUpdate("Counter", 0.25f);

    Assert.Equal(1, target.Fixeds);
    Assert.Equal(0.25f, target.LastDt);
    Assert.Equal(0, sibling.Fixeds);
  }

  [Fact]
  public void LifecycleCallsForAnUnknownKeyAreNoOps()
  {
    var present = Seed<Counter>("Counter");

    Call(&Bridge.start, "Missing");
    Call(&Bridge.variableUpdate, "Missing");
    Call(&Bridge.stop, "Missing");
    Call(&Bridge.detachScript, "Missing");
    CallFixedUpdate("Missing", 1f);
    Call(&Bridge.start, "Counter");

    Assert.Equal((1, 0), (present.Starts, present.Fixeds));
  }

  [Fact]
  public void ThrowingFixedUpdate_FaultsOnlyItsOwnKeyAndStopStillRuns()
  {
    var faulty = Seed<Counter>("Counter");
    var healthy = Seed<Counter>("Counter", Other);
    faulty.ThrowInFixed = true;

    CallFixedUpdate("Counter", 0.1f);
    Call(&Bridge.variableUpdate, "Counter");
    Call(&Bridge.variableUpdate, "Counter", Other);
    Call(&Bridge.stop, "Counter");

    Assert.Equal((1, 0, 1), (faulty.Fixeds, faulty.Variables, faulty.Stops));
    Assert.Equal(1, healthy.Variables);
    Assert.Equal(0, Healthy("Counter"));
    Assert.Equal(1, Healthy("Counter", Other));
  }

  [Theory]
  [InlineData(0, "enter:other-\u00e9")]
  [InlineData(1, "stay:other-\u00e9")]
  [InlineData(2, "exit:other-\u00e9")]
  public void OnCollision_ReachesTheMatchingHandlerWithTheOtherUuidIntact(int eventType, string expected)
  {
    var counter = Seed<Counter>("Counter");
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, int, void> onCollision = &Bridge.onCollision;
    using var u = new NativeUtf8(Uuid);
    using var c = new NativeUtf8("Counter");
    using var other = new NativeUtf8("other-\u00e9");

    onCollision(u.Ptr, c.Ptr, other.Ptr, eventType);

    Assert.Equal(new[] { expected }, counter.Collisions);
  }

  [Fact]
  public void OnCollision_UnknownEventTypeDoesNothing()
  {
    var counter = Seed<Counter>("Counter");
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, int, void> onCollision = &Bridge.onCollision;
    using var u = new NativeUtf8(Uuid);
    using var c = new NativeUtf8("Counter");
    using var other = new NativeUtf8(Other);

    onCollision(u.Ptr, c.Ptr, other.Ptr, 9);
    Assert.Empty(counter.Collisions);

    onCollision(u.Ptr, c.Ptr, other.Ptr, 0);
    Assert.Single(counter.Collisions);
  }

  [Fact]
  public void OnSceneEvent_ReachesASceneScriptAndIsIgnoredByAnObjectScript()
  {
    var scene = Seed<SceneRecorder>("Recorder");
    Seed<Counter>("Counter");
    delegate* unmanaged<IntPtr, IntPtr, int, IntPtr, int, void> onSceneEvent = &Bridge.onSceneEvent;
    using var u = new NativeUtf8(Uuid);
    using var recorder = new NativeUtf8("Recorder");
    using var counter = new NativeUtf8("Counter");
    using var spawned = new NativeUtf8("spawned-\u00e9");
    using var none = new NativeUtf8("");

    onSceneEvent(u.Ptr, recorder.Ptr, 0, none.Ptr, 0);
    onSceneEvent(u.Ptr, recorder.Ptr, 2, spawned.Ptr, 0);
    onSceneEvent(u.Ptr, recorder.Ptr, 4, none.Ptr, 3);
    onSceneEvent(u.Ptr, recorder.Ptr, 99, none.Ptr, 0);
    onSceneEvent(u.Ptr, counter.Ptr, 0, none.Ptr, 0);

    Assert.Equal(new[] { "loaded", "spawned:spawned-\u00e9", "joined:3" }, scene.Events);
    Assert.Equal(1, Healthy("Counter"));
  }

  [Fact]
  public void GetExposedFields_ListsTheExposedFieldsAndIsEmptyForAMissingInstance()
  {
    Seed<Exposed>("Exposed");
    delegate* unmanaged<IntPtr, IntPtr, IntPtr> getExposedFields = &Bridge.getExposedFields;
    string present;
    string missing;
    using (var u = new NativeUtf8(Uuid))
    using (var c = new NativeUtf8("Exposed"))
    using (var absent = new NativeUtf8("Absent"))
    {
      present = TakeString(getExposedFields(u.Ptr, c.Ptr));
      missing = TakeString(getExposedFields(u.Ptr, absent.Ptr));
    }

    using var doc = JsonDocument.Parse(present);
    var summary = new List<string>();
    foreach (var field in doc.RootElement.EnumerateArray())
    {
      summary.Add($"{field.GetProperty("name").GetString()}|{field.GetProperty("displayName").GetString()}|" +
                  field.GetProperty("type").GetString());
    }

    summary.Sort(StringComparer.Ordinal);
    Assert.Equal(new[] { "Flag|Flag|bool", "Label|Label|string", "Number|Count|int", "Offset|Offset|vector3",
                         "Speed|Speed|float" }, summary);
    Assert.Equal("[]", missing);
  }

  [Fact]
  public void FloatIntAndBoolFields_RoundTripThroughTheEntryPoints()
  {
    var script = Seed<Exposed>("Exposed");
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, float, void> setFloat = &Bridge.setFieldFloat;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, float> getFloat = &Bridge.getFieldFloat;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, int, void> setInt = &Bridge.setFieldInt;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, int> getInt = &Bridge.getFieldInt;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, byte, void> setBool = &Bridge.setFieldBool;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, byte> getBool = &Bridge.getFieldBool;
    using var u = new NativeUtf8(Uuid);
    using var c = new NativeUtf8("Exposed");
    using var speed = new NativeUtf8("Speed");
    using var number = new NativeUtf8("Number");
    using var flag = new NativeUtf8("Flag");

    setFloat(u.Ptr, c.Ptr, speed.Ptr, 2.5f);
    setInt(u.Ptr, c.Ptr, number.Ptr, -7);
    setBool(u.Ptr, c.Ptr, flag.Ptr, 1);

    Assert.Equal((2.5f, -7, true), (script.Speed, script.Number, script.Flag));
    Assert.Equal(2.5f, getFloat(u.Ptr, c.Ptr, speed.Ptr));
    Assert.Equal(-7, getInt(u.Ptr, c.Ptr, number.Ptr));
    Assert.Equal(1, getBool(u.Ptr, c.Ptr, flag.Ptr));

    setBool(u.Ptr, c.Ptr, flag.Ptr, 0);

    Assert.False(script.Flag);
    Assert.Equal(0, getBool(u.Ptr, c.Ptr, flag.Ptr));
  }

  [Fact]
  public void StringField_RoundTripsNonAsciiTextThroughTheEntryPoints()
  {
    var script = Seed<Exposed>("Exposed");
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, IntPtr, void> setString = &Bridge.setFieldString;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, IntPtr> getString = &Bridge.getFieldString;
    using var u = new NativeUtf8(Uuid);
    using var c = new NativeUtf8("Exposed");
    using var label = new NativeUtf8("Label");
    using var value = new NativeUtf8("caf\u00e9 \u4e16\u754c");

    setString(u.Ptr, c.Ptr, label.Ptr, value.Ptr);

    Assert.Equal("caf\u00e9 \u4e16\u754c", script.Label);
    Assert.Equal("caf\u00e9 \u4e16\u754c", TakeString(getString(u.Ptr, c.Ptr, label.Ptr)));
  }

  [Fact]
  public void Vector3Field_RoundTripsThroughTheEntryPoints()
  {
    var script = Seed<Exposed>("Exposed");
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, float, float, float, void> setVector = &Bridge.setFieldVector3;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, float*, float*, float*, void> getVector = &Bridge.getFieldVector3;
    using var u = new NativeUtf8(Uuid);
    using var c = new NativeUtf8("Exposed");
    using var offset = new NativeUtf8("Offset");
    float x = -1f, y = -1f, z = -1f;

    setVector(u.Ptr, c.Ptr, offset.Ptr, 1f, 2f, 3f);
    getVector(u.Ptr, c.Ptr, offset.Ptr, &x, &y, &z);

    Assert.Equal(new Vector3(1f, 2f, 3f), script.Offset);
    Assert.Equal(new Vector3(1f, 2f, 3f), new Vector3(x, y, z));
  }

  [Fact]
  public void GettingFieldsOfAMissingInstance_ReadsDefaults()
  {
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, float> getFloat = &Bridge.getFieldFloat;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, int> getInt = &Bridge.getFieldInt;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, byte> getBool = &Bridge.getFieldBool;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, IntPtr> getString = &Bridge.getFieldString;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, float*, float*, float*, void> getVector = &Bridge.getFieldVector3;
    using var u = new NativeUtf8(Uuid);
    using var c = new NativeUtf8("Missing");
    using var field = new NativeUtf8("Speed");
    float x = 9f, y = 9f, z = 9f;

    getVector(u.Ptr, c.Ptr, field.Ptr, &x, &y, &z);

    Assert.Equal(0f, getFloat(u.Ptr, c.Ptr, field.Ptr));
    Assert.Equal(0, getInt(u.Ptr, c.Ptr, field.Ptr));
    Assert.Equal(0, getBool(u.Ptr, c.Ptr, field.Ptr));
    Assert.Equal("", TakeString(getString(u.Ptr, c.Ptr, field.Ptr)));
    Assert.Equal(Vector3.Zero, new Vector3(x, y, z));
  }

  [Fact]
  public void SettingAFieldWithTheWrongType_IsRefusedWithoutFaultingTheScript()
  {
    var script = Seed<Exposed>("Exposed");
    script.Number = 4;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, IntPtr, void> setString = &Bridge.setFieldString;
    delegate* unmanaged<IntPtr, IntPtr, IntPtr, int, void> setInt = &Bridge.setFieldInt;
    using var u = new NativeUtf8(Uuid);
    using var c = new NativeUtf8("Exposed");
    using var number = new NativeUtf8("Number");
    using var text = new NativeUtf8("12");

    setString(u.Ptr, c.Ptr, number.Ptr, text.Ptr);

    Assert.Equal(4, script.Number);
    Assert.Equal(1, Healthy("Exposed"));

    setInt(u.Ptr, c.Ptr, number.Ptr, 9);

    Assert.Equal(9, script.Number);
  }

  [UnmanagedCallersOnly]
  private static void FakeLogWrite(int level, IntPtr message)
  {
    // Only its address is compared.
  }

  [UnmanagedCallersOnly]
  private static void FakeTransformStart(IntPtr uuid)
  {
    // Only its address is compared.
  }

  [Fact]
  public void RegisterBindings_StoreTheBindingsTheNativeSideHandsOver()
  {
    var previousLog = NativeBindings.Log;
    var previousTransform = NativeBindings.Transform;
    try
    {
      delegate* unmanaged<LogBindings, void> registerLog = &Bridge.registerLogBindings;
      delegate* unmanaged<TransformBindings, void> registerTransform = &Bridge.registerTransformBindings;
      delegate* unmanaged<int, IntPtr, void> fakeWrite = &FakeLogWrite;
      delegate* unmanaged<IntPtr, void> fakeStart = &FakeTransformStart;
      var log = new LogBindings { write = fakeWrite };
      var transform = new TransformBindings { start = fakeStart };

      registerLog(log);
      registerTransform(transform);

      Assert.True(NativeBindings.Log.write == fakeWrite);
      Assert.True(NativeBindings.Transform.start == fakeStart);
    }
    finally
    {
      NativeBindings.Log = previousLog;
      NativeBindings.Transform = previousTransform;
    }
  }
}
