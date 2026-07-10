using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
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

    // Script-to-script access. An object can carry several scripts (one per class), so a script is
    // addressed by type (FindScript<T>) or enumerated for the untyped case (FindScripts). Purely a view
    // over the live instances — ScriptBase exposes these as getScript<T>/getScripts to user scripts.
    internal static T? FindScript<T>(string uuid) where T : ScriptBase =>
        _instances.Values.OfType<T>().FirstOrDefault(s => s.EntityId == uuid);

    internal static IReadOnlyList<ScriptBase> FindScripts(string uuid) =>
        _instances.Values.Where(s => s.EntityId == uuid).ToList();

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
    public static byte getFieldBool(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr)
        => (byte)(((bool)(GetField(uuidPtr, classNamePtr, fieldNamePtr) ?? false)) ? 1 : 0);

    [UnmanagedCallersOnly]
    public static unsafe void getFieldVector3(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr,
                                              float* x, float* y, float* z)
    {
        var v = (Vector3)(GetField(uuidPtr, classNamePtr, fieldNamePtr) ?? Vector3.Zero);
        *x = v.X;
        *y = v.Y;
        *z = v.Z;
    }

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
    public static void setFieldBool(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr, byte value)
        => SetField(uuidPtr, classNamePtr, fieldNamePtr, value != 0);

    [UnmanagedCallersOnly]
    public static void setFieldVector3(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr,
                                       float x, float y, float z)
        => SetField(uuidPtr, classNamePtr, fieldNamePtr, new Vector3(x, y, z));

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

        if (field == null)
        {
            return;
        }

        // Convert.ChangeType only handles IConvertible (float/int/bool); a struct like Vector3 arrives as
        // the field's own type already, so assign it directly.
        var converted = field.FieldType.IsInstanceOfType(value) ? value : Convert.ChangeType(value, field.FieldType);
        field.SetValue(instance, converted);
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

        if (t == typeof(Vector3))
        {
            return "vector3";
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
    public static unsafe void registerWorldBindings(WorldBindings bindings)
    {
        NativeBindings.World = bindings;
    }

    [UnmanagedCallersOnly]
    public static unsafe void registerCameraBindings(CameraBindings bindings)
    {
        NativeBindings.Camera = bindings;
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
    }

    [UnmanagedCallersOnly]
    public static void detachScript(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var key = Key(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
        if (_instances.TryGetValue(key, out var instance))
        {
            _instances.Remove(key);
        }
    }

    [UnmanagedCallersOnly]
    public static void start(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var key = Key(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
        if (_instances.TryGetValue(key, out var instance))
        {
            instance.start();
        }
    }

    [UnmanagedCallersOnly]
    public static void stop(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var key = Key(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
        if (_instances.TryGetValue(key, out var instance))
        {
            instance.stop();
        }
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

    // eventType matches CollisionEvent in ScriptSystem.h: 0 = enter, 1 = stay, 2 = exit.
    [UnmanagedCallersOnly]
    public static void onCollision(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr otherUuidPtr, int eventType)
    {
        var key = Key(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
        if (!_instances.TryGetValue(key, out var instance))
        {
            return;
        }

        var other = Marshal.PtrToStringUTF8(otherUuidPtr)!;
        switch (eventType)
        {
            case 0: instance.onCollisionEnter(other); break;
            case 1: instance.onCollisionStay(other); break;
            case 2: instance.onCollisionExit(other); break;
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
