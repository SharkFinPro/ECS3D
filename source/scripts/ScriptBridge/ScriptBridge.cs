using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace ScriptBridge;

public static class Bridge
{
  private static string _scriptDir = "";
  private static ScriptContext? _ctx;

  [UnmanagedCallersOnly]
  public static void init(IntPtr scriptDirPtr)
  {
    _scriptDir = Marshal.PtrToStringUTF8(scriptDirPtr)
           ?? throw new ArgumentNullException(nameof(scriptDirPtr));

    Console.WriteLine($"[Bridge] Script directory: {_scriptDir}");
    CompileAndLoad();
  }

  [UnmanagedCallersOnly]
  public static void reloadScripts()
  {
    if (_ctx is { } ctx)
    {
      foreach (var s in ctx.Scripts)
      {
        try { s.stop(); } catch { }
      }

      ctx.Unload();
      _ctx = null;

      GC.Collect();
      GC.WaitForPendingFinalizers();
    }

    CompileAndLoad();
  }

  [UnmanagedCallersOnly]
  public static void fixedUpdate(float dt)
  {
    if (_ctx is null)
    {
      return;
    }

    foreach (var script in _ctx.Scripts)
    {
      try
      {
        script.fixedUpdate(dt);
      }
      catch (Exception ex)
      {
        Console.Error.WriteLine($"[Bridge] OnUpdate error in {script.GetType().Name}: {ex.Message}");
      }
    }
  }

  [UnmanagedCallersOnly]
  public static void variableUpdate(float dt)
  {
    if (_ctx is null)
    {
      return;
    }

    foreach (var script in _ctx.Scripts)
    {
      try
      {
        script.variableUpdate();
      }
      catch (Exception ex)
      {
        Console.Error.WriteLine($"[Bridge] OnUpdate error in {script.GetType().Name}: {ex.Message}");
      }
    }
  }

  private static void CompileAndLoad()
  {
    if (!Directory.Exists(_scriptDir))
    {
      Console.Error.WriteLine($"[Bridge] Script directory not found: {_scriptDir}");
      return;
    }

    var sourceFiles = Directory.GetFiles(_scriptDir, "*.cs", SearchOption.AllDirectories);
    if (sourceFiles.Length == 0)
    {
      Console.WriteLine("[Bridge] No .cs files found in script directory.");
      return;
    }

    Console.WriteLine($"[Bridge] Compiling {sourceFiles.Length} script file(s)...");

    var syntaxTrees = sourceFiles
      .Select(path => CSharpSyntaxTree.ParseText(File.ReadAllText(path), path: path))
      .ToArray();

    var compilation = CSharpCompilation.Create(
      assemblyName: "UserScripts_" + Guid.NewGuid().ToString("N")[..8],
      syntaxTrees: syntaxTrees,
      references: Directory.GetFiles(Path.GetDirectoryName(typeof(object).Assembly.Location)!, "*.dll")
              .Where(dll => {
                try { System.Reflection.AssemblyName.GetAssemblyName(dll); return true; }
                catch { return false; }
              })
              .Select(dll => (MetadataReference)MetadataReference.CreateFromFile(dll))
              .Append(MetadataReference.CreateFromFile(typeof(ScriptBase).Assembly.Location))
              .ToArray(),
      options: new CSharpCompilationOptions(
        OutputKind.DynamicallyLinkedLibrary,
        optimizationLevel: OptimizationLevel.Debug,
        nullableContextOptions: NullableContextOptions.Enable));

    using var ms = new MemoryStream();
    var result = compilation.Emit(ms);

    foreach (var diag in result.Diagnostics.Where(d => d.Severity >= DiagnosticSeverity.Warning))
    {
      Console.WriteLine($"[Bridge]   {diag}");
    }

    if (!result.Success)
    {
      Console.Error.WriteLine("[Bridge] Compilation failed:");
      foreach (var e in result.Diagnostics.Where(d => d.Severity == DiagnosticSeverity.Error))
      {
        Console.Error.WriteLine($"  {e}");
      }
      return;
    }

    Console.WriteLine("[Bridge] Compilation succeeded.");

    ms.Seek(0, SeekOrigin.Begin);
    _ctx = new ScriptContext(ms);

    Console.WriteLine($"[Bridge] Loaded {_ctx.Scripts.Length} script(s):");
    foreach (var s in _ctx.Scripts)
    {
      Console.WriteLine($"  + {s.GetType().Name}");
      try
      {
        s.start();
      }
      catch (Exception ex)
      {
        Console.Error.WriteLine($"[Bridge] OnStart error in {s.GetType().Name}: {ex.Message}");
      }
    }
  }
}

internal sealed class ScriptContext : AssemblyLoadContext
{
  public ScriptBase[] Scripts { get; }

  protected override Assembly? Load(AssemblyName name)
  {
    if (name.Name == typeof(ScriptBase).Assembly.GetName().Name)
    {
      return typeof(ScriptBase).Assembly;
    }

    return null;
  }

  public ScriptContext(Stream dll) : base(isCollectible: true)
  {
    var asm = LoadFromStream(dll);

    Scripts = asm.GetTypes()
      .Where(t => t.IsSubclassOf(typeof(ScriptBase)) && !t.IsAbstract && t.IsPublic)
      .Select(t => Activator.CreateInstance(t) as ScriptBase)
      .OfType<ScriptBase>()
      .ToArray();
  }
}
