using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace ScriptBridge;

public static class Bridge
{
    private static string _scriptDir = "";
    private static ScriptContext? _ctx;

    private static readonly Dictionary<string, ScriptBase> _instances = new();
    private static string Key(string uuid, string className) => $"{uuid}_{className}";

    private static string ReadFileSafe(string path)
    {
        using var fs = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.ReadWrite | FileShare.Delete
        );

        using var reader = new StreamReader(fs);
        return reader.ReadToEnd();
    }

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
        foreach (var instance in _instances.Values)
        {
            try { instance.stop(); } catch {}
        }

        _instances.Clear();

        _ctx?.Unload();
        _ctx = null;

        GC.Collect();
        GC.WaitForPendingFinalizers();

        CompileAndLoad();
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
            .Select(path => CSharpSyntaxTree.ParseText(ReadFileSafe(path), path: path))
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
            Console.WriteLine($"[Bridge]     {diag}");
        }

        if (!result.Success)
        {
            Console.Error.WriteLine("[Bridge] Compilation failed:");
            foreach (var e in result.Diagnostics.Where(d => d.Severity == DiagnosticSeverity.Error))
            {
                Console.Error.WriteLine($"    {e}");
            }
            return;
        }

        Console.WriteLine("[Bridge] Compilation succeeded.");

        ms.Seek(0, SeekOrigin.Begin);
        _ctx = new ScriptContext(ms);

        Console.WriteLine($"[Bridge] {_ctx.ScriptTypes.Length} script type(s) available.");
        foreach (var t in _ctx.ScriptTypes)
        {
            Console.WriteLine($"    + {t.Name}");
        }
    }

    [UnmanagedCallersOnly]
    public static IntPtr getExposedFields(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var key = Key(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
        if (!_instances.TryGetValue(key, out var instance))
        {
            return Marshal.StringToCoTaskMemUTF8("[]");
        }

        var fields = instance.GetType()
            .GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
            .Where(f => f.GetCustomAttribute<ExposeToEditorAttribute>() != null && MapTypeName(f.FieldType) != null)
            .Select(f => new {
                name        = f.Name,
                displayName = f.GetCustomAttribute<ExposeToEditorAttribute>()!.DisplayName ?? f.Name,
                type        = MapTypeName(f.FieldType)!
            })
            .ToArray();

        return Marshal.StringToCoTaskMemUTF8(JsonSerializer.Serialize(fields));
    }

    [UnmanagedCallersOnly]
    public static void freeString(IntPtr ptr) => Marshal.FreeCoTaskMem(ptr);

    [UnmanagedCallersOnly]
    public static float getFieldFloat(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr)
        => (float)(GetField(uuidPtr, classNamePtr, fieldNamePtr) ?? 0f);

    [UnmanagedCallersOnly]
    public static int getFieldInt(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr)
        => (int)(GetField(uuidPtr, classNamePtr, fieldNamePtr) ?? 0);

    [UnmanagedCallersOnly]
    public static bool getFieldBool(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr)
        => (bool)(GetField(uuidPtr, classNamePtr, fieldNamePtr) ?? false);

    private static object? GetField(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr)
    {
        var key = Key(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
        var fieldName = Marshal.PtrToStringUTF8(fieldNamePtr)!;

        if (!_instances.TryGetValue(key, out var instance))
        {
            return null;
        }

        return instance.GetType()
            .GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
            .FirstOrDefault(f => f.Name == fieldName && f.GetCustomAttribute<ExposeToEditorAttribute>() != null)
            ?.GetValue(instance);
    }

    [UnmanagedCallersOnly]
    public static void setFieldFloat(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr, float value)
        => SetField(uuidPtr, classNamePtr, fieldNamePtr, value);

    [UnmanagedCallersOnly]
    public static void setFieldInt(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr, int value)
        => SetField(uuidPtr, classNamePtr, fieldNamePtr, value);

    [UnmanagedCallersOnly]
    public static void setFieldBool(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr,
                                    [MarshalAs(UnmanagedType.U1)] bool value)
        => SetField(uuidPtr, classNamePtr, fieldNamePtr, value);

    private static void SetField(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr, object value)
    {
        var key = Key(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
        var fieldName = Marshal.PtrToStringUTF8(fieldNamePtr)!;

        if (!_instances.TryGetValue(key, out var instance))
        {
            return;
        }
        var field = instance.GetType()
            .GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
            .FirstOrDefault(f => f.Name == fieldName && f.GetCustomAttribute<ExposeToEditorAttribute>() != null);

        field?.SetValue(instance, Convert.ChangeType(value, field.FieldType));
    }

    private static string? MapTypeName(Type t)
    {
        if (t == typeof(float))
        {
            return "float";
        }

        if (t == typeof(int))
        {
            return "int";
        }

        if (t == typeof(bool))
        {
            return "bool";
        }

        if (t == typeof(string))
        {
            return "string";
        }

        return null;
    }

    [UnmanagedCallersOnly]
    public static unsafe void registerInputUtilsBindings(InputUtilsBindings bindings)
    {
        NativeBindings.InputUtils = bindings;
    }

    [UnmanagedCallersOnly]
    public static unsafe void registerRigidBodyBindings(RigidBodyBindings bindings)
    {
        NativeBindings.RigidBody = bindings;
    }

    [UnmanagedCallersOnly]
    public static unsafe void registerTransformBindings(TransformBindings bindings)
    {
        NativeBindings.Transform = bindings;
    }

    [UnmanagedCallersOnly]
    public static void attachScript(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        var type = _ctx?.FindType(className);
        if (type is null)
        {
            return;
        }

        var instance = (ScriptBase)Activator.CreateInstance(type)!;
        instance.EntityId = uuid;
        instance.initComponents();
        _instances[Key(uuid, className)] = instance;
        instance.start();
    }

    [UnmanagedCallersOnly]
    public static void detachScript(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var key = Key(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
        if (!_instances.TryGetValue(key, out var instance))
        {
            return;
        }

        instance.stop();
        _instances.Remove(key);
    }

    [UnmanagedCallersOnly]
    public static void fixedUpdate(IntPtr uuidPtr, IntPtr classNamePtr, float dt)
    {
        var key = Key(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
        if (_instances.TryGetValue(key, out var instance))
        {
            instance.fixedUpdate(dt);
        }
    }

    [UnmanagedCallersOnly]
    public static void variableUpdate(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var key = Key(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
        if (_instances.TryGetValue(key, out var instance))
        {
            instance.variableUpdate();
        }
    }
}

internal sealed class ScriptContext : AssemblyLoadContext
{
    public Type[] ScriptTypes { get; }

    public Type? FindType(string name) => ScriptTypes.FirstOrDefault(t => t.Name == name);

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

        ScriptTypes = asm.GetTypes()
            .Where(t => t.IsSubclassOf(typeof(ScriptBase)) && !t.IsAbstract && t.IsPublic)
            .ToArray();
    }
}
