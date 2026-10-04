using System;
using System.Collections.Generic;
using System.Numerics;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Bridge's instance table is static, so every class that touches it shares this collection and runs
// serially.
[CollectionDefinition("BridgeInstances", DisableParallelization = true)]
public class BridgeInstancesCollection
{
}

// Script-to-script lookup and ScriptHandle over Bridge's instance table. Instances are added through
// Bridge.AddInstance, which does not construct the native component wrappers, so no CLR host is needed.
[Collection("BridgeInstances")]
public class ScriptLookupTests : IDisposable
{
  public class Target : ScriptBase
  {
    [ExposeToEditor("Speed")]
    private float speed = 2.5f;

    [ExposeToEditor]
    private int count = 4;

    [ExposeToEditor("Offset")]
    private Vector3 offset = new(1f, 2f, 3f);

    [ExposeToEditor("Label")]
    private string label = "hi";

    [ExposeToEditor("Unsupported")]
    private double unsupported = 1.0;

    private readonly float notExposed = 99f;

    public int Prop { get; set; } = 1;

    public int Calls { get; private set; }
    public int StartCalls { get; private set; }
    public int FixedCalls { get; private set; }

    public Target()
    {
    }

    public Target(float speed)
    {
      this.speed = speed;
    }

    public void Ping() => Calls++;

    public int Add(int a, int b) => a + b;

    public float Scale(float value) => value * 2f;

    public string Describe(int value) => $"int:{value}";

    public string Describe(string value) => $"string:{value}";

    public string Both(int value) => "int";

    public string Both(object value) => "object";

    public string Nullable(string? value) => value ?? "null";

    public int NullableInt(int value) => value;

    public void Explode() => throw new InvalidOperationException("boom");

    public override void start() => StartCalls++;

    public override void fixedUpdate(float dt) => FixedCalls++;

    public override string ToString() => "Target";
  }

  public class Other : ScriptBase
  {
    public int Calls { get; private set; }

    public void Ping() => Calls++;
  }

  private readonly string _uuid = Guid.NewGuid().ToString();
  private readonly List<(string uuid, string className)> _added = new();

  private T Add<T>(T instance, string? uuid = null) where T : ScriptBase
  {
    var id = uuid ?? _uuid;
    var className = typeof(T).Name;
    Bridge.AddInstance(id, className, instance);
    _added.Add((id, className));
    return instance;
  }

  public void Dispose()
  {
    foreach (var (uuid, className) in _added)
    {
      Bridge.RemoveInstance(uuid, className);
    }
  }

  private ScriptHandle Handle(string className = nameof(Target))
  {
    Assert.True(Bridge.TryFindScript(_uuid, className, out var handle));
    return handle;
  }

  [Fact]
  public void FindByName_FindsInstanceAndRecordsIdentity()
  {
    Add(new Target());

    Assert.True(Bridge.TryFindScript(_uuid, nameof(Target), out var handle));
    Assert.Equal(_uuid, handle.EntityId);
    Assert.Equal(nameof(Target), handle.ClassName);
    Assert.True(handle.isAlive);
  }

  [Fact]
  public void FindByName_FalseForWrongClassUnknownUuidAndAfterRemoval()
  {
    Add(new Target());
    Assert.True(Bridge.TryFindScript(_uuid, nameof(Target), out _));

    Assert.False(Bridge.TryFindScript(_uuid, nameof(Other), out var wrongClass));
    Assert.Null(wrongClass);
    Assert.False(Bridge.TryFindScript(Guid.NewGuid().ToString(), nameof(Target), out _));

    Bridge.RemoveInstance(_uuid, nameof(Target));
    Assert.False(Bridge.TryFindScript(_uuid, nameof(Target), out _));
  }

  [Fact]
  public void FindTyped_FindsMatchingTypeOnly()
  {
    var target = Add(new Target());

    Assert.True(Bridge.TryFindScript<Target>(_uuid, out var found));
    Assert.Same(target, found);

    Assert.False(Bridge.TryFindScript<Other>(_uuid, out var wrong));
    Assert.Null(wrong);
  }

  [Fact]
  public void TryGetField_ReadsExposedFieldsOfEachSupportedType()
  {
    Add(new Target());
    var handle = Handle();

    Assert.True(handle.tryGetField("speed", out float speed));
    Assert.Equal(2.5f, speed);
    Assert.True(handle.tryGetField("count", out int count));
    Assert.Equal(4, count);
    Assert.True(handle.tryGetField("offset", out Vector3 offset));
    Assert.Equal(new Vector3(1f, 2f, 3f), offset);
    Assert.True(handle.tryGetField("label", out string label));
    Assert.Equal("hi", label);
  }

  [Fact]
  public void TryGetField_RefusesNonExposedMismatchedAndMissing()
  {
    Add(new Target());
    var handle = Handle();

    Assert.True(handle.tryGetField("speed", out float _));
    Assert.False(handle.tryGetField("unsupported", out double _));
    Assert.False(handle.tryGetField("notExposed", out float notExposed));
    Assert.Equal(0f, notExposed);

    Assert.False(handle.tryGetField("count", out float _));
    Assert.False(handle.tryGetField("speed", out int _));
    Assert.False(handle.tryGetField("missing", out float _));
  }

  [Fact]
  public void ExposedFieldNames_ListsExposedSupportedFieldsOnly()
  {
    Add(new Target());

    var names = Handle().exposedFieldNames;

    Assert.Equivalent(new[] { "speed", "count", "offset", "label" }, names);
  }

  [Fact]
  public void TryInvoke_CallsVoidMethod()
  {
    var target = Add(new Target());

    Assert.True(Handle().tryInvoke("Ping"));
    Assert.Equal(1, target.Calls);
  }

  [Fact]
  public void TryInvoke_ReturnsValueThroughResult()
  {
    Add(new Target());

    Assert.True(Handle().tryInvoke("Add", out var result, 2, 3));
    Assert.Equal(5, result);
  }

  [Fact]
  public void TryInvoke_VoidMethodYieldsNullResult()
  {
    Add(new Target());

    Assert.True(Handle().tryInvoke("Ping", out var result));
    Assert.Null(result);
  }

  [Fact]
  public void TryInvoke_PicksOverloadByArgumentType()
  {
    Add(new Target());
    var handle = Handle();

    Assert.True(handle.tryInvoke("Describe", out var fromInt, 7));
    Assert.Equal("int:7", fromInt);
    Assert.True(handle.tryInvoke("Describe", out var fromString, "x"));
    Assert.Equal("string:x", fromString);
  }

  [Fact]
  public void TryInvoke_RefusesWrongArgumentCountOrType()
  {
    var target = Add(new Target());
    var handle = Handle();

    Assert.True(handle.tryInvoke("Add", 1, 2));
    Assert.False(handle.tryInvoke("Add", 1));
    Assert.False(handle.tryInvoke("Add", 1, "two"));
    Assert.False(handle.tryInvoke("Ping", 1));
    Assert.Equal(0, target.Calls);
  }

  [Fact]
  public void TryInvoke_RefusesAmbiguousOverloads()
  {
    Add(new Target());
    var handle = Handle();

    Assert.False(handle.tryInvoke("Both", out var ambiguous, 5));
    Assert.Null(ambiguous);

    Assert.True(handle.tryInvoke("Both", out var unambiguous, "text"));
    Assert.Equal("object", unambiguous);
  }

  [Fact]
  public void TryInvoke_NullArgumentMatchesReferenceAndNullableParametersOnly()
  {
    Add(new Target());
    var handle = Handle();

    Assert.True(handle.tryInvoke("Nullable", out var viaNull, new object?[] { null }));
    Assert.Equal("null", viaNull);
    Assert.False(handle.tryInvoke("NullableInt", new object?[] { null }));
    Assert.True(handle.tryInvoke("NullableInt", out var viaInt, 3));
    Assert.Equal(3, viaInt);
  }

  [Fact]
  public void TryInvoke_RefusesMissingMethod()
  {
    Add(new Target());

    Assert.True(Handle().tryInvoke("Ping"));
    Assert.False(Handle().tryInvoke("NoSuchMethod"));
  }

  [Fact]
  public void TryInvoke_RefusesPropertyAccessors()
  {
    var target = Add(new Target());
    var handle = Handle();

    Assert.True(handle.tryInvoke("Ping"));
    Assert.False(handle.tryInvoke("get_Prop"));
    Assert.False(handle.tryInvoke("set_Prop", 5));
    Assert.Equal(1, target.Prop);
  }

  [Fact]
  public void TryInvoke_RequiresExactParameterTypes()
  {
    Add(new Target());
    var handle = Handle();

    Assert.True(handle.tryInvoke("Scale", out var scaled, 1.5f));
    Assert.Equal(3f, scaled);
    Assert.False(handle.tryInvoke("Scale", out var widened, 2));
    Assert.Null(widened);
  }

  [Fact]
  public void TryInvoke_RefusesScriptBaseLifecycleAndObjectMembers()
  {
    var target = Add(new Target());
    var handle = Handle();

    Assert.True(handle.tryInvoke("Ping"));
    Assert.False(handle.tryInvoke("start"));
    Assert.False(handle.tryInvoke("fixedUpdate", 0.1f));
    Assert.False(handle.tryInvoke("ToString"));
    Assert.Equal(0, target.StartCalls);
    Assert.Equal(0, target.FixedCalls);
  }

  [Fact]
  public void TryInvoke_ThrowingMethodFaultsTargetOnly()
  {
    var target = Add(new Target());
    var other = Add(new Other());
    var handle = Handle();
    var otherHandle = Handle(nameof(Other));

    var thrown = Record.Exception(() => Assert.False(handle.tryInvoke("Explode")));

    Assert.Null(thrown);
    Assert.False(handle.isAlive);
    Assert.False(Bridge.TryFindScript(_uuid, nameof(Target), out _));
    Assert.False(handle.tryInvoke("Ping"));
    Assert.Equal(0, target.Calls);

    Assert.True(otherHandle.isAlive);
    Assert.True(otherHandle.tryInvoke("Ping"));
    Assert.Equal(1, other.Calls);
  }

  [Fact]
  public void Handle_DeadAfterRemovalAndRefusesEverything()
  {
    var target = Add(new Target());
    var handle = Handle();
    Assert.True(handle.isAlive);

    Bridge.RemoveInstance(_uuid, nameof(Target));

    Assert.False(handle.isAlive);
    Assert.False(handle.tryGetField("speed", out float _));
    Assert.False(handle.tryInvoke("Ping"));
    Assert.Empty(handle.exposedFieldNames);
    Assert.Equal(0, target.Calls);
  }

  [Fact]
  public void Handle_StaleHandleDoesNotReachReplacementInstance()
  {
    var first = Add(new Target());
    var stale = Handle();

    var second = Add(new Target(8f));
    var fresh = Handle();

    Assert.False(stale.isAlive);
    Assert.False(stale.tryInvoke("Ping"));
    Assert.False(stale.tryGetField("speed", out float _));
    Assert.Equal(0, first.Calls);

    Assert.True(fresh.isAlive);
    Assert.True(fresh.tryInvoke("Ping"));
    Assert.Equal(1, second.Calls);
    Assert.True(fresh.tryGetField("speed", out float speed));
    Assert.Equal(8f, speed);
  }
}
