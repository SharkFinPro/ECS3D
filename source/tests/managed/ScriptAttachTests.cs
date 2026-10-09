using System;
using System.Collections.Generic;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Instance creation and health over Bridge's static instance table, reached through the internal helpers
// behind the [UnmanagedCallersOnly] entry points. Shares the BridgeInstances collection with the other
// classes that touch that table.
[Collection("BridgeInstances")]
public class ScriptAttachTests : IDisposable
{
  public class Healthy : ScriptBase
  {
    public void Explode() => throw new InvalidOperationException("boom");
  }

  public class ThrowsInConstructor : ScriptBase
  {
    public ThrowsInConstructor() => throw new InvalidOperationException("ctor");
  }

  private readonly string _uuid = Guid.NewGuid().ToString();
  private readonly List<string> _classNames = new();

  private string Track(string className)
  {
    _classNames.Add(className);
    return className;
  }

  public void Dispose()
  {
    foreach (var className in _classNames)
    {
      Bridge.RemoveInstance(_uuid, className);
    }
  }

  [Fact]
  public void TryCreateInstance_NullTypeReturnsFalseAndAddsNothing()
  {
    var className = Track("Missing");

    Assert.False(Bridge.TryCreateInstance(null, _uuid, className));

    Assert.False(Bridge.IsHealthy(_uuid, className));
    Assert.False(Bridge.TryFindScript(_uuid, className, out _));
  }

  [Fact]
  public void TryCreateInstance_RealTypeReturnsTrueAndIsFindable()
  {
    var className = Track(nameof(Healthy));

    Assert.True(Bridge.TryCreateInstance(typeof(Healthy), _uuid, className));

    Assert.True(Bridge.IsHealthy(_uuid, className));
    Assert.True(Bridge.TryFindScript(_uuid, className, out _));
  }

  [Fact]
  public void TryCreateInstance_ThrowingConstructorReturnsFalseAndAddsNothing()
  {
    var className = Track(nameof(ThrowsInConstructor));

    var thrown = Record.Exception(() => Assert.False(Bridge.TryCreateInstance(typeof(ThrowsInConstructor), _uuid, className)));

    Assert.Null(thrown);
    Assert.False(Bridge.IsHealthy(_uuid, className));
    Assert.False(Bridge.TryFindScript(_uuid, className, out _));
  }

  [Fact]
  public void IsHealthy_FalseForMissingKeyTrueForLiveFalseOnceFaulted()
  {
    var className = Track(nameof(Healthy));
    Assert.False(Bridge.IsHealthy(_uuid, className));

    var instance = new Healthy();
    Bridge.AddInstance(_uuid, className, instance);
    Assert.True(Bridge.IsHealthy(_uuid, className));

    Assert.False(Bridge.TryInvokeScript(_uuid, className, instance, "Explode", Array.Empty<object?>(), out _));

    Assert.False(Bridge.IsHealthy(_uuid, className));
  }

  [Fact]
  public void IsHealthy_IsPerKey()
  {
    var healthyName = Track(nameof(Healthy));
    var otherName = Track("Other");
    var instance = new Healthy();
    Bridge.AddInstance(_uuid, healthyName, instance);
    Bridge.AddInstance(_uuid, otherName, new Healthy());

    Bridge.TryInvokeScript(_uuid, healthyName, instance, "Explode", Array.Empty<object?>(), out _);

    Assert.False(Bridge.IsHealthy(_uuid, healthyName));
    Assert.True(Bridge.IsHealthy(_uuid, otherName));
  }
}
