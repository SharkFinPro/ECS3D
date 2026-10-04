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
    internal static string Key(string uuid, string className) => $"{uuid}_{className}";

    // A script that throws once must not take the process down, and must not spam the log every tick
    // after that. Once a key is here, every entry point below skips it silently instead of calling in;
    // reloadScripts and detachScript are the only places that clear it, since those are the only ways a
    // faulted script's code can change or go away.
    private static readonly HashSet<string> _faulted = new();

    // Log.error can itself throw (its fallback writes to Console.Error, which can be a closed/redirected
    // handle in a service context), and that must not escape any further than the fault it was reporting
    // would have. Best effort only - if this fails too there is nothing left to do without risking the
    // same escape again.
    private static void SafeWriteError(string message)
    {
        try
        {
            Log.error(message);
        }
        catch
        {
        }
    }

    // Reading ex.Message/ex.StackTrace can itself throw - a user script can throw a custom Exception
    // subclass with a hostile override - so the message is built in its own try and falls back to a
    // hardcoded string that touches nothing on the exception if that happens.
    private static void ReportFault(string uuid, string className, string methodName, Exception ex)
    {
        string message;
        try
        {
            message = $"Script '{className}' on object {uuid} threw in {methodName}; the script has been stopped.\n" +
                      $"{ex.GetType().Name}: {ex.Message}\n{ex.StackTrace}";
        }
        catch
        {
            message = $"Script '{className}' on object {uuid} threw in {methodName}, and the exception " +
                      "could not be described; the script has been stopped.";
        }

        SafeWriteError(message);
    }

    // Same reporting shape as ReportFault, worded for reloadScripts()'s stop-everything sweep: the script
    // isn't being marked faulted here (reloadScripts clears _faulted right after), it's just being told
    // about instead of silently swallowed the way this line used to.
    private static void ReportReloadCleanupFault(string uuid, string className, Exception ex)
    {
        string message;
        try
        {
            message = $"Script '{className}' on object {uuid} threw in stop() during reload cleanup; " +
                      $"continuing the reload.\n{ex.GetType().Name}: {ex.Message}\n{ex.StackTrace}";
        }
        catch
        {
            message = $"Script '{className}' on object {uuid} threw in stop() during reload cleanup, and " +
                      "the exception could not be described; continuing the reload.";
        }

        SafeWriteError(message);
    }

    // Runs a user-script call under the fault gate: skipped if this instance already faulted, and on a
    // caught exception the instance is marked faulted (so later calls skip too) and the failure is
    // reported once. Returns whether the action actually ran. bypassFaultGate lets a caller run its own
    // cleanup even on an already-faulted instance (stop() needs this - see its call site).
    private static bool RunGuarded(string uuid, string className, string methodName, Action action,
                                   bool bypassFaultGate = false)
    {
        var key = Key(uuid, className);
        if (!bypassFaultGate && _faulted.Contains(key))
        {
            return false;
        }

        try
        {
            action();
            return true;
        }
        catch (Exception ex)
        {
            _faulted.Add(key);
            ReportFault(uuid, className, methodName, ex);
            return false;
        }
    }

    // Same as RunGuarded, for entry points that must hand back a value. fallback is whatever the caller
    // already treats as "no result" - reused rather than inventing a new sentinel.
    private static T RunGuarded<T>(string uuid, string className, string methodName, Func<T> func, T fallback)
    {
        var key = Key(uuid, className);
        if (_faulted.Contains(key))
        {
            return fallback;
        }

        try
        {
            return func();
        }
        catch (Exception ex)
        {
            _faulted.Add(key);
            ReportFault(uuid, className, methodName, ex);
            return fallback;
        }
    }

    // Script-to-script access. An object can carry several scripts (one per class), so a script is
    // addressed by type (FindScript<T>), by class name (TryFindScript, which hands back a ScriptHandle),
    // or enumerated for the untyped case (FindScripts). Purely a view over the live instances - exposed
    // to user scripts through ScriptBase.getScript<T>/getScripts and World.tryGetScript.
    // Faulted instances are excluded: handing one out would let a sibling script call into it directly,
    // bypassing the fault gate, and a throw from that call would fault the wrong script (the caller).
    // A typed instance is still called without the gate; ScriptHandle.tryInvoke is the gated route.
    internal static T? FindScript<T>(string uuid) where T : ScriptBase =>
        _instances
            .Where(kvp => !_faulted.Contains(kvp.Key) && kvp.Value.EntityId == uuid)
            .Select(kvp => kvp.Value)
            .OfType<T>()
            .FirstOrDefault();

    internal static bool TryFindScript<T>(string uuid, out T script) where T : ScriptBase
    {
        var found = FindScript<T>(uuid);
        script = found!;
        return found != null;
    }

    internal static bool TryFindScript(string uuid, string className, out ScriptHandle handle)
    {
        var key = Key(uuid, className);
        if (_faulted.Contains(key) || !_instances.TryGetValue(key, out var instance))
        {
            handle = null!;
            return false;
        }

        handle = new ScriptHandle(uuid, className, instance);
        return true;
    }

    // True only while this exact instance is still the one registered under its key and has not faulted,
    // so a handle kept across a detach, a re-attach or a reload reads as dead.
    internal static bool IsLive(string uuid, string className, ScriptBase instance)
    {
        var key = Key(uuid, className);
        return !_faulted.Contains(key) &&
               _instances.TryGetValue(key, out var current) &&
               ReferenceEquals(current, instance);
    }

    internal static void AddInstance(string uuid, string className, ScriptBase instance)
    {
        instance.EntityId = uuid;
        _instances[Key(uuid, className)] = instance;
    }

    internal static void RemoveInstance(string uuid, string className)
    {
        var key = Key(uuid, className);
        _instances.Remove(key);

        // A removed script is gone regardless of fault state; clear it so a later re-add of the same
        // uuid/class gets a clean slate instead of being skipped forever.
        _faulted.Remove(key);
    }

    // Methods a script may drive from another script by name: public instance methods the target's own
    // type adds. ScriptBase's lifecycle and event virtuals (and object's members) stay with ScriptSystem,
    // even when the target overrides them - GetBaseDefinition finds where the slot was first declared.
    private static bool IsInvokableFromScript(MethodInfo method)
    {
        if (method.IsGenericMethodDefinition || method.IsSpecialName)
        {
            return false;
        }

        var root = method.GetBaseDefinition().DeclaringType;
        return root != typeof(ScriptBase) && root != typeof(object);
    }

    private static bool AcceptsArgument(Type parameterType, object? argument)
    {
        if (parameterType.IsByRef)
        {
            return false;
        }

        if (argument == null)
        {
            return !parameterType.IsValueType || Nullable.GetUnderlyingType(parameterType) != null;
        }

        return parameterType.IsInstanceOfType(argument);
    }

    // Invokes a public method on another script's instance by name, under the target's fault gate: a
    // throw faults the target (not the caller) and returns false. A name that matches no method, or
    // more than one for these arguments, is a caller mistake - warned about, not a fault.
    internal static bool TryInvokeScript(string uuid, string className, ScriptBase instance, string methodName,
                                         object?[] args, out object? result)
    {
        result = null;
        if (!IsLive(uuid, className, instance))
        {
            return false;
        }

        var candidates = instance.GetType()
            .GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .Where(m => m.Name == methodName && IsInvokableFromScript(m))
            .Where(m =>
            {
                var parameters = m.GetParameters();
                return parameters.Length == args.Length &&
                       parameters.Select((p, i) => AcceptsArgument(p.ParameterType, args[i])).All(ok => ok);
            })
            .ToArray();

        if (candidates.Length != 1)
        {
            Log.warn($"Script '{className}' on object {uuid}: {(candidates.Length == 0 ? "no" : "more than one")} " +
                     $"invokable method '{methodName}' takes {args.Length} argument(s) of those types.");
            return false;
        }

        try
        {
            result = candidates[0].Invoke(instance, args);
            return true;
        }
        catch (Exception ex)
        {
            _faulted.Add(Key(uuid, className));
            ReportFault(uuid, className, methodName,
                        ex is TargetInvocationException { InnerException: { } inner } ? inner : ex);
            result = null;
            return false;
        }
    }

    internal static IReadOnlyList<ScriptBase> FindScripts(string uuid) =>
        _instances
            .Where(kvp => !_faulted.Contains(kvp.Key) && kvp.Value.EntityId == uuid)
            .Select(kvp => kvp.Value)
            .ToList();

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

        Log.info($"Script directory: {_scriptDir}");
        CompileAndLoad();
    }

    [UnmanagedCallersOnly]
    public static void reloadScripts()
    {
        foreach (var instance in _instances.Values)
        {
            try
            {
                instance.stop();
            }
            catch (Exception ex)
            {
                ReportReloadCleanupFault(instance.EntityId, instance.GetType().Name, ex);
            }
        }

        _instances.Clear();
        _faulted.Clear();

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
            Log.error($"Script directory not found: {_scriptDir}");
            return;
        }

        var sourceFiles = Directory.GetFiles(_scriptDir, "*.cs", SearchOption.AllDirectories);
        if (sourceFiles.Length == 0)
        {
            Log.warn("No .cs files found in script directory.");
            return;
        }

        Log.info($"Compiling {sourceFiles.Length} script file(s)...");

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
            Log.info($"    {diag}");
        }

        if (!result.Success)
        {
            Log.error("Compilation failed:");
            foreach (var e in result.Diagnostics.Where(d => d.Severity == DiagnosticSeverity.Error))
            {
                Log.error($"    {e}");
            }
            return;
        }

        Log.info("Compilation succeeded.");

        ms.Seek(0, SeekOrigin.Begin);
        _ctx = new ScriptContext(ms);

        Log.info($"{_ctx.ScriptTypes.Length} script type(s) available.");
        foreach (var t in _ctx.ScriptTypes)
        {
            Log.info($"    + {t.Name}");
        }
    }

    [UnmanagedCallersOnly]
    public static IntPtr getExposedFields(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        var key = Key(uuid, className);
        if (_faulted.Contains(key) || !_instances.TryGetValue(key, out var instance))
        {
            return Marshal.StringToCoTaskMemUTF8("[]");
        }

        try
        {
            return Marshal.StringToCoTaskMemUTF8(BuildExposedFieldsJson(instance));
        }
        catch (Exception ex)
        {
            _faulted.Add(key);
            ReportFault(uuid, className, nameof(getExposedFields), ex);
            return Marshal.StringToCoTaskMemUTF8("[]");
        }
    }

    // The reflection + JSON side of getExposedFields, pulled out of the [UnmanagedCallersOnly] entry
    // point so it is reachable without an IntPtr uuid/className pair or a live _instances entry.
    internal static string BuildExposedFieldsJson(object instance)
    {
        var fields = SupportedExposedFields(instance)
            .Select(f => new {
                name        = f.Name,
                displayName = f.GetCustomAttribute<ExposeToEditorAttribute>()!.DisplayName ?? f.Name,
                type        = MapTypeName(f.FieldType)!
            })
            .ToArray();

        return JsonSerializer.Serialize(fields);
    }

    // The [ExposeToEditor] fields of a supported type: what BuildExposedFieldsJson reports and what
    // ScriptHandle.exposedFieldNames lists.
    internal static IEnumerable<FieldInfo> SupportedExposedFields(object instance) =>
        instance.GetType()
            .GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
            .Where(f => f.GetCustomAttribute<ExposeToEditorAttribute>() != null && MapTypeName(f.FieldType) != null);

    // ReadExposedField limited to the supported types, so a read agrees with SupportedExposedFields.
    internal static bool TryReadSupportedExposedField(object instance, string fieldName, out object? value)
    {
        var field = SupportedExposedFields(instance).FirstOrDefault(f => f.Name == fieldName);
        value = field?.GetValue(instance);
        return field != null;
    }

    [UnmanagedCallersOnly]
    public static void freeString(IntPtr ptr) => Marshal.FreeCoTaskMem(ptr);

    [UnmanagedCallersOnly]
    public static float getFieldFloat(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        return RunGuarded(uuid, className, nameof(getFieldFloat),
            () => (float)(GetField(uuidPtr, classNamePtr, fieldNamePtr) ?? 0f), 0f);
    }

    [UnmanagedCallersOnly]
    public static int getFieldInt(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        return RunGuarded(uuid, className, nameof(getFieldInt),
            () => (int)(GetField(uuidPtr, classNamePtr, fieldNamePtr) ?? 0), 0);
    }

    [UnmanagedCallersOnly]
    public static byte getFieldBool(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        return RunGuarded(uuid, className, nameof(getFieldBool),
            () => (byte)(((bool)(GetField(uuidPtr, classNamePtr, fieldNamePtr) ?? false)) ? 1 : 0), (byte)0);
    }

    [UnmanagedCallersOnly]
    public static unsafe void getFieldVector3(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr,
                                              float* x, float* y, float* z)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        var v = RunGuarded(uuid, className, nameof(getFieldVector3),
            () => (Vector3)(GetField(uuidPtr, classNamePtr, fieldNamePtr) ?? Vector3.Zero), Vector3.Zero);
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

        return ReadExposedField(instance, fieldName);
    }

    // Finds an [ExposeToEditor] field by name, shared by the read and write paths below - pulled out of
    // GetField/SetField so it is reachable without an IntPtr uuid/className pair or a live _instances
    // entry.
    internal static FieldInfo? FindExposedField(object instance, string fieldName) =>
        instance.GetType()
            .GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
            .FirstOrDefault(f => f.Name == fieldName && f.GetCustomAttribute<ExposeToEditorAttribute>() != null);

    internal static object? ReadExposedField(object instance, string fieldName) =>
        FindExposedField(instance, fieldName)?.GetValue(instance);

    [UnmanagedCallersOnly]
    public static void setFieldFloat(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr, float value)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        RunGuarded(uuid, className, nameof(setFieldFloat), () => SetField(uuidPtr, classNamePtr, fieldNamePtr, value));
    }

    [UnmanagedCallersOnly]
    public static void setFieldInt(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr, int value)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        RunGuarded(uuid, className, nameof(setFieldInt), () => SetField(uuidPtr, classNamePtr, fieldNamePtr, value));
    }

    [UnmanagedCallersOnly]
    public static void setFieldBool(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr, byte value)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        RunGuarded(uuid, className, nameof(setFieldBool), () => SetField(uuidPtr, classNamePtr, fieldNamePtr, value != 0));
    }

    [UnmanagedCallersOnly]
    public static void setFieldVector3(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr,
                                       float x, float y, float z)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        RunGuarded(uuid, className, nameof(setFieldVector3),
            () => SetField(uuidPtr, classNamePtr, fieldNamePtr, new Vector3(x, y, z)));
    }

    private static void SetField(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr fieldNamePtr, object value)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        var key = Key(uuid, className);
        var fieldName = Marshal.PtrToStringUTF8(fieldNamePtr)!;

        if (!_instances.TryGetValue(key, out var instance))
        {
            return;
        }
        var field = FindExposedField(instance, fieldName);

        if (field == null)
        {
            return;
        }

        if (!TryConvertFieldValue(field.FieldType, value, out var converted))
        {
            // Bad input, not a script fault: letting this reach RunGuarded would fault the instance
            // and skip every later call for it.
            Log.warn($"Refused a field edit on {uuid} ({className}): field '{fieldName}' is " +
                     $"{field.FieldType.Name}, got {value.GetType().Name}");
            return;
        }

        field.SetValue(instance, converted);
    }

    // Convert.ChangeType only handles IConvertible (float/int/bool); a struct like Vector3 arrives as the
    // field's own type already, so it is assigned directly rather than routed through ChangeType. Pulled
    // out of SetField so the conversion/refusal rule is reachable without an IntPtr uuid/className pair.
    internal static bool TryConvertFieldValue(Type fieldType, object value, out object? converted)
    {
        if (fieldType.IsInstanceOfType(value))
        {
            converted = value;
            return true;
        }

        try
        {
            converted = Convert.ChangeType(value, fieldType);
            return true;
        }
        catch (Exception ex) when (ex is InvalidCastException or FormatException or OverflowException)
        {
            converted = null;
            return false;
        }
    }

    internal static string? MapTypeName(Type t)
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
    public static unsafe void registerLogBindings(LogBindings bindings)
    {
        NativeBindings.Log = bindings;
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
    public static unsafe void registerComponentOpsBindings(ComponentOpsBindings bindings)
    {
        NativeBindings.ComponentOps = bindings;
    }

    [UnmanagedCallersOnly]
    public static unsafe void registerCameraBindings(CameraBindings bindings)
    {
        NativeBindings.Camera = bindings;
    }

    [UnmanagedCallersOnly]
    public static unsafe void registerColliderBindings(ColliderBindings bindings)
    {
        NativeBindings.Collider = bindings;
    }

    [UnmanagedCallersOnly]
    public static unsafe void registerModelRendererBindings(ModelRendererBindings bindings)
    {
        NativeBindings.ModelRenderer = bindings;
    }

    [UnmanagedCallersOnly]
    public static unsafe void registerLightRendererBindings(LightRendererBindings bindings)
    {
        NativeBindings.LightRenderer = bindings;
    }

    [UnmanagedCallersOnly]
    public static unsafe void registerPlayerControllerBindings(PlayerControllerBindings bindings)
    {
        NativeBindings.PlayerController = bindings;
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

        // A throwing constructor (Activator.CreateInstance) is the same hazard as a throwing script
        // method, so it goes through the same fault gate even though no instance exists yet.
        RunGuarded(uuid, className, nameof(attachScript), () =>
        {
            var instance = (ScriptBase)Activator.CreateInstance(type)!;
            instance.EntityId = uuid;
            instance.initComponents();
            AddInstance(uuid, className, instance);
        });
    }

    [UnmanagedCallersOnly]
    public static void detachScript(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        RemoveInstance(Marshal.PtrToStringUTF8(uuidPtr)!, Marshal.PtrToStringUTF8(classNamePtr)!);
    }

    [UnmanagedCallersOnly]
    public static void start(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        if (_instances.TryGetValue(Key(uuid, className), out var instance))
        {
            RunGuarded(uuid, className, nameof(start), () => instance.start());
        }
    }

    [UnmanagedCallersOnly]
    public static void stop(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        if (_instances.TryGetValue(Key(uuid, className), out var instance))
        {
            // Bypasses the fault gate: native ScriptSystem::stop calls this then unconditionally detaches,
            // so it is the one place a faulted instance's cleanup hook still has to run - otherwise
            // whatever it registered or allocated in start() leaks for the life of the process. The
            // instance is not removed from _instances or _faulted here; it stays resident until
            // detachScript runs the normal cleanup path rather than firing stop() from inside a catch
            // block on an object whose invariants may already be broken.
            RunGuarded(uuid, className, nameof(stop), () => instance.stop(), bypassFaultGate: true);
        }
    }

    [UnmanagedCallersOnly]
    public static void fixedUpdate(IntPtr uuidPtr, IntPtr classNamePtr, float dt)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        if (_instances.TryGetValue(Key(uuid, className), out var instance))
        {
            RunGuarded(uuid, className, nameof(fixedUpdate), () => instance.fixedUpdate(dt));
        }
    }

    [UnmanagedCallersOnly]
    public static void variableUpdate(IntPtr uuidPtr, IntPtr classNamePtr)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        if (_instances.TryGetValue(Key(uuid, className), out var instance))
        {
            RunGuarded(uuid, className, nameof(variableUpdate), () => instance.variableUpdate());
        }
    }

    // eventType matches CollisionEvent in ScriptSystem.h: 0 = enter, 1 = stay, 2 = exit.
    [UnmanagedCallersOnly]
    public static void onCollision(IntPtr uuidPtr, IntPtr classNamePtr, IntPtr otherUuidPtr, int eventType)
    {
        var uuid = Marshal.PtrToStringUTF8(uuidPtr)!;
        var className = Marshal.PtrToStringUTF8(classNamePtr)!;
        if (!_instances.TryGetValue(Key(uuid, className), out var instance))
        {
            return;
        }

        var other = Marshal.PtrToStringUTF8(otherUuidPtr)!;
        RunGuarded(uuid, className, nameof(onCollision), () =>
        {
            switch (eventType)
            {
                case 0: instance.onCollisionEnter(other); break;
                case 1: instance.onCollisionStay(other); break;
                case 2: instance.onCollisionExit(other); break;
            }
        });
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
