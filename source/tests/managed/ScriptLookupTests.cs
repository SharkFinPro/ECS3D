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
    public float speed = 2.5f;

    [ExposeToEditor]
    public int count = 4;

    [ExposeToEditor("Offset")]
    public Vector3 offset = new(1f, 2f, 3f);

    [ExposeToEditor("Label")]
    public string label = "hi";

    [ExposeToEditor("Unsupported")]
    public double unsupported = 1.0;

    public float notExposed = 99f;

    public int calls;
    public int startCalls;
    public int fixedCalls;

    public void Ping() => calls++;

    public int Add(int a, int b) => a + b;

    public string Describe(int value) => $"int:{value}";

    public string Describe(string value) => $"string:{value}";

    public string Both(int value) => "int";

    public string Both(object value) => "object";

    public string Nullable(string? value) => value ?? "null";

    public int NullableInt(int value) => value;

    public void Explode() => throw new InvalidOperationException("boom");

    public override void start() => startCalls++;

    public override void fixedUpdate(float dt) => fixedCalls++;

    public override string ToString() => "Target";
  }

  public class Other : ScriptBase
  {
    public int calls;

    public void Ping() => calls++;
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
    Assert.Equal(1, target.calls);
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
    Assert.Equal(0, target.calls);
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
  public void TryInvoke_RefusesScriptBaseLifecycleAndObjectMembers()
  {
    var target = Add(new Target());
    var handle = Handle();

    Assert.True(handle.tryInvoke("Ping"));
    Assert.False(handle.tryInvoke("start"));
    Assert.False(handle.tryInvoke("fixedUpdate", 0.1f));
    Assert.False(handle.tryInvoke("ToString"));
    Assert.Equal(0, target.startCalls);
    Assert.Equal(0, target.fixedCalls);
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
    Assert.Equal(0, target.calls);

    Assert.True(otherHandle.isAlive);
    Assert.True(otherHandle.tryInvoke("Ping"));
    Assert.Equal(1, other.calls);
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
    Assert.Equal(0, target.calls);
  }

  [Fact]
  public void Handle_StaleHandleDoesNotReachReplacementInstance()
  {
    var first = Add(new Target());
    var stale = Handle();

    var second = Add(new Target { speed = 8f });
    var fresh = Handle();

    Assert.False(stale.isAlive);
    Assert.False(stale.tryInvoke("Ping"));
    Assert.False(stale.tryGetField("speed", out float _));
    Assert.Equal(0, first.calls);

    Assert.True(fresh.isAlive);
    Assert.True(fresh.tryInvoke("Ping"));
    Assert.Equal(1, second.calls);
    Assert.True(fresh.tryGetField("speed", out float speed));
    Assert.Equal(8f, speed);
  }
}
