using System;
using System.IO;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// The fault gate and the init/reload sweep over Bridge's static state. Shares the BridgeInstances
// collection with ScriptLookupTests, and resets that state around every test.
[Collection("BridgeInstances")]
public class BridgeLifecycleTests : IDisposable
{
  private class StopCounter : ScriptBase
  {
    public int StopCalls { get; private set; }

    public override void stop() => StopCalls++;
  }

  private class StopThrower : ScriptBase
  {
    public override void stop() => throw new InvalidOperationException("stop failed");
  }

  public sealed class HostileException : Exception
  {
    public override string Message => throw new InvalidOperationException("message is hostile");
  }

  private const string Uuid = "lifecycle-uuid";
  private const string ClassName = "LifecycleScript";

  private readonly string _dir = Path.Combine(Path.GetTempPath(), "ecs3d-bridge-" + Guid.NewGuid().ToString("N"));

  public BridgeLifecycleTests()
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

  private void WriteScript(string fileName, string source)
  {
    Directory.CreateDirectory(_dir);
    File.WriteAllText(Path.Combine(_dir, fileName), source);
  }

  private static string ScriptSource(string className) =>
    $"using ScriptBridge;\npublic class {className} : ScriptBase {{ }}\n";

  private static void Fault(string uuid, string className)
  {
    Assert.False(Bridge.RunGuarded(uuid, className, "test", () => throw new InvalidOperationException("boom")));
  }

  [Fact]
  public void RunGuarded_ThrowingActionReturnsFalseAndFaultsKey()
  {
    var calls = 0;

    var ran = Bridge.RunGuarded(Uuid, ClassName, "test", () =>
    {
      calls++;
      throw new InvalidOperationException("boom");
    });

    Assert.False(ran);
    Assert.Equal(1, calls);

    Assert.False(Bridge.RunGuarded(Uuid, ClassName, "test", () => calls++));
    Assert.Equal(1, calls);
  }

  [Fact]
  public void RunGuarded_FaultDoesNotAffectOtherKeys()
  {
    var calls = 0;
    Fault(Uuid, ClassName);

    Assert.True(Bridge.RunGuarded(Uuid, "OtherClass", "test", () => calls++));
    Assert.True(Bridge.RunGuarded("other-uuid", ClassName, "test", () => calls++));
    Assert.Equal(2, calls);
    Assert.False(Bridge.RunGuarded(Uuid, ClassName, "test", () => calls++));
    Assert.Equal(2, calls);
  }

  [Fact]
  public void RunGuarded_SuccessfulActionReturnsTrueAndKeepsRunning()
  {
    var calls = 0;

    Assert.True(Bridge.RunGuarded(Uuid, ClassName, "test", () => calls++));
    Assert.True(Bridge.RunGuarded(Uuid, ClassName, "test", () => calls++));
    Assert.Equal(2, calls);
  }

  [Fact]
  public void RunGuarded_BypassRunsOnFaultedKey()
  {
    var calls = 0;
    Fault(Uuid, ClassName);
    Assert.False(Bridge.RunGuarded(Uuid, ClassName, "test", () => calls++));
    Assert.Equal(0, calls);

    Assert.True(Bridge.RunGuarded(Uuid, ClassName, "test", () => calls++, bypassFaultGate: true));
    Assert.Equal(1, calls);
  }

  [Fact]
  public void RunGuarded_BypassStillContainsAThrow()
  {
    Fault(Uuid, ClassName);

    var thrown = Record.Exception(() => Assert.False(
      Bridge.RunGuarded(Uuid, ClassName, "test", () => throw new InvalidOperationException("again"),
                        bypassFaultGate: true)));

    Assert.Null(thrown);
  }

  [Fact]
  public void RunGuardedWithResult_ReturnsValueUntilFaulted()
  {
    Assert.Equal(7, Bridge.RunGuarded(Uuid, ClassName, "test", () => 7, -1));

    var thrown = Bridge.RunGuarded<int>(Uuid, ClassName, "test", () => throw new InvalidOperationException("boom"), -1);

    Assert.Equal(-1, thrown);
    Assert.Equal(-1, Bridge.RunGuarded(Uuid, ClassName, "test", () => 7, -1));
    Assert.Equal(7, Bridge.RunGuarded("other-uuid", ClassName, "test", () => 7, -1));
  }

  [Fact]
  public void RunGuardedWithResult_FaultedKeySkipsTheFunction()
  {
    var calls = 0;
    Fault(Uuid, ClassName);

    var value = Bridge.RunGuarded(Uuid, ClassName, "test", () =>
    {
      calls++;
      return 7;
    }, -1);

    Assert.Equal(-1, value);
    Assert.Equal(0, calls);
  }

  [Fact]
  public void RunGuarded_ExceptionWithThrowingMessageIsStillContained()
  {
    var calls = 0;

    var thrown = Record.Exception(() => Assert.False(
      Bridge.RunGuarded(Uuid, ClassName, "test", () => throw new HostileException())));

    Assert.Null(thrown);
    Assert.False(Bridge.RunGuarded(Uuid, ClassName, "test", () => calls++));
    Assert.Equal(0, calls);
    Assert.True(Bridge.RunGuarded(Uuid, "OtherClass", "test", () => calls++));
    Assert.Equal(1, calls);
  }

  [Fact]
  public void Initialize_CompilesScriptsFromDirectory()
  {
    WriteScript("script.cs", ScriptSource(ClassName));

    Bridge.Initialize(_dir);

    Assert.Equal(new[] { ClassName }, Bridge.LoadedScriptTypeNames);
  }

  [Fact]
  public void Initialize_MissingDirectoryLeavesLoadedTypesAsTheyWere()
  {
    var loadedDir = Path.Combine(_dir, "loaded");
    Directory.CreateDirectory(loadedDir);
    File.WriteAllText(Path.Combine(loadedDir, "script.cs"), ScriptSource(ClassName));
    Bridge.Initialize(loadedDir);
    Assert.Equal(new[] { ClassName }, Bridge.LoadedScriptTypeNames);

    var thrown = Record.Exception(() => Bridge.Initialize(Path.Combine(_dir, "missing")));

    Assert.Null(thrown);
    Assert.Equal(new[] { ClassName }, Bridge.LoadedScriptTypeNames);
  }

  [Fact]
  public void Initialize_EmptyDirectoryLeavesLoadedTypesAsTheyWere()
  {
    var loadedDir = Path.Combine(_dir, "loaded");
    var emptyDir = Path.Combine(_dir, "empty");
    Directory.CreateDirectory(loadedDir);
    Directory.CreateDirectory(emptyDir);
    File.WriteAllText(Path.Combine(loadedDir, "script.cs"), ScriptSource(ClassName));
    Bridge.Initialize(loadedDir);
    Assert.Equal(new[] { ClassName }, Bridge.LoadedScriptTypeNames);

    var thrown = Record.Exception(() => Bridge.Initialize(emptyDir));

    Assert.Null(thrown);
    Assert.Equal(new[] { ClassName }, Bridge.LoadedScriptTypeNames);
  }

  [Fact]
  public void Reload_ClearsInstancesAndFaultsAndLoadsNewTypes()
  {
    WriteScript("script.cs", ScriptSource("FirstScript"));
    Bridge.Initialize(_dir);
    Assert.Equal(new[] { "FirstScript" }, Bridge.LoadedScriptTypeNames);

    var healthy = new StopCounter();
    Bridge.AddInstance(Uuid, "Healthy", healthy);
    Bridge.AddInstance(Uuid, "Faulted", new StopCounter());
    Fault(Uuid, "Faulted");
    Assert.True(Bridge.TryFindScript(Uuid, "Healthy", out _));

    WriteScript("script.cs", ScriptSource("SecondScript"));
    Bridge.Reload();

    Assert.Equal(1, healthy.StopCalls);
    Assert.False(Bridge.TryFindScript(Uuid, "Healthy", out _));
    Assert.False(Bridge.TryFindScript(Uuid, "Faulted", out _));
    Assert.Equal(new[] { "SecondScript" }, Bridge.LoadedScriptTypeNames);

    // A fresh instance under the previously faulted key is live again, so the fault set was cleared.
    Bridge.AddInstance(Uuid, "Faulted", new StopCounter());
    Assert.True(Bridge.TryFindScript(Uuid, "Faulted", out _));
  }

  [Fact]
  public void Reload_ThrowingStopIsContainedAndOthersStillStop()
  {
    WriteScript("script.cs", ScriptSource(ClassName));
    Bridge.Initialize(_dir);

    var first = new StopCounter();
    var second = new StopCounter();
    Bridge.AddInstance(Uuid, "First", first);
    Bridge.AddInstance(Uuid, "Thrower", new StopThrower());
    Bridge.AddInstance(Uuid, "Second", second);

    var thrown = Record.Exception(() => Bridge.Reload());

    Assert.Null(thrown);
    Assert.Equal(1, first.StopCalls);
    Assert.Equal(1, second.StopCalls);
    Assert.False(Bridge.TryFindScript(Uuid, "Thrower", out _));
    Assert.Equal(new[] { ClassName }, Bridge.LoadedScriptTypeNames);
  }

  [Fact]
  public void Reload_StopWithThrowingMessageIsContained()
  {
    WriteScript("script.cs", ScriptSource(ClassName));
    Bridge.Initialize(_dir);
    var after = new StopCounter();
    Bridge.AddInstance(Uuid, "Hostile", new HostileStop());
    Bridge.AddInstance(Uuid, "After", after);

    var thrown = Record.Exception(() => Bridge.Reload());

    Assert.Null(thrown);
    Assert.Equal(1, after.StopCalls);
    Assert.Equal(new[] { ClassName }, Bridge.LoadedScriptTypeNames);
  }

  private class UpdateCounter : ScriptBase
  {
    public int Updates { get; private set; }

    public override void fixedUpdate(float dt) => Updates++;
  }

  [Fact]
  public void Reload_CompileErrorKeepsPreviousTypesInstancesAndFaults()
  {
    WriteScript("script.cs", ScriptSource(ClassName));
    Bridge.Initialize(_dir);
    var instance = new UpdateCounter();
    Bridge.AddInstance(Uuid, ClassName, instance);
    Bridge.AddInstance(Uuid, "Faulted", new StopCounter());
    Fault(Uuid, "Faulted");
    Assert.True(Bridge.IsHealthy(Uuid, ClassName));

    WriteScript("script.cs", "using ScriptBridge;
public class Broken : ScriptBase { int x = ; }
");
    var replaced = Bridge.Reload();

    Assert.False(replaced);
    Assert.Equal(new[] { ClassName }, Bridge.LoadedScriptTypeNames);
    Assert.True(Bridge.TryFindScript(Uuid, ClassName, out _));
    Assert.True(Bridge.IsHealthy(Uuid, ClassName));
    Assert.False(Bridge.IsHealthy(Uuid, "Faulted"));
    Assert.True(Bridge.RunGuarded(Uuid, ClassName, "test", () => instance.fixedUpdate(0.1f)));
    Assert.Equal(1, instance.Updates);
  }

  [Fact]
  public void Reload_AfterDeletingAllSourcesUnloadsTheOldScripts()
  {
    WriteScript("script.cs", ScriptSource(ClassName));
    Bridge.Initialize(_dir);
    var instance = new StopCounter();
    Bridge.AddInstance(Uuid, ClassName, instance);

    File.Delete(Path.Combine(_dir, "script.cs"));

    Assert.True(Bridge.Reload());
    Assert.Equal(1, instance.StopCalls);
    Assert.Empty(Bridge.LoadedScriptTypeNames);
    Assert.False(Bridge.TryFindScript(Uuid, ClassName, out _));
  }

  [Fact]
  public void Reload_AfterDeletingTheDirectoryUnloadsTheOldScripts()
  {
    WriteScript("script.cs", ScriptSource(ClassName));
    Bridge.Initialize(_dir);
    Bridge.AddInstance(Uuid, ClassName, new StopCounter());

    Directory.Delete(_dir, recursive: true);

    Assert.True(Bridge.Reload());
    Assert.Empty(Bridge.LoadedScriptTypeNames);
    Assert.False(Bridge.TryFindScript(Uuid, ClassName, out _));
  }

  [Fact]
  public void Reload_SuccessReportsTheReplacement()
  {
    WriteScript("script.cs", ScriptSource("FirstScript"));
    Bridge.Initialize(_dir);
    var instance = new StopCounter();
    Bridge.AddInstance(Uuid, "First", instance);

    WriteScript("script.cs", ScriptSource("SecondScript"));

    Assert.True(Bridge.Reload());
    Assert.Equal(1, instance.StopCalls);
    Assert.False(Bridge.TryFindScript(Uuid, "First", out _));
    Assert.Equal(new[] { "SecondScript" }, Bridge.LoadedScriptTypeNames);
  }

  [Fact]
  public void Reload_RecoversOnceTheSourceCompilesAgain()
  {
    WriteScript("script.cs", "using ScriptBridge;\npublic class Broken : ScriptBase { int x = ; }\n");
    Bridge.Initialize(_dir);
    Assert.Empty(Bridge.LoadedScriptTypeNames);

    WriteScript("script.cs", ScriptSource(ClassName));
    Bridge.Reload();

    Assert.Equal(new[] { ClassName }, Bridge.LoadedScriptTypeNames);
  }

  private class HostileStop : ScriptBase
  {
    public override void stop() => throw new HostileException();
  }
}
