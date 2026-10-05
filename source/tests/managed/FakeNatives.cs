using System;
using System.Collections.Generic;
using System.Globalization;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;

namespace ECS3DManagedTests;

// Stand-ins for the native binding tables the C++ side fills at startup. Install() points every
// NativeBindings.* struct at these functions, which record each call in Calls (as "Struct.fn(arg,arg)", a
// uuid or other string argument read back from its UTF-8 pointer) and answer from the values a test put in
// with Set/SetString. Disposing the returned scope restores the previous tables and frees what SetString
// allocated.
//
// ScriptBridge disables runtime marshalling, so its bool is the native 1-byte bool; this assembly does not,
// and an [UnmanagedCallersOnly] method cannot take or return a non-blittable bool. The fakes use byte and the
// tables are filled through a cast to the bool signature, which has the same one-byte shape. The bool tests
// therefore prove routing, not the DisableRuntimeMarshalling ABI: a 1-byte fake cannot put garbage in the
// upper bits of the return register.
internal static unsafe partial class FakeNatives
{
  internal static readonly List<string> Calls = new();

  // A mistyped Set is recorded here, since throwing inside an UnmanagedCallersOnly fake would abort the host.
  internal static string? LastError;

  private static readonly Dictionary<string, object> Canned = new();

  private static readonly List<IntPtr> Allocated = new();

  internal static void Set(string key, object value) => Canned[key] = value;

  // A null string is answered as a null pointer, which the wrappers read as "".
  internal static void SetString(string key, string? value)
  {
    if (value == null)
    {
      Canned[key] = IntPtr.Zero;
      return;
    }

    var ptr = Marshal.StringToCoTaskMemUTF8(value);
    Allocated.Add(ptr);
    Canned[key] = ptr;
  }

  internal static IDisposable Install() => new Scope();

  internal static bool IsInstalled => NativeBindings.Transform.has != null;

  private sealed class Scope : IDisposable
  {
    private readonly InputUtilsBindings _inputUtils = NativeBindings.InputUtils;
    private readonly RigidBodyBindings _rigidBody = NativeBindings.RigidBody;
    private readonly TransformBindings _transform = NativeBindings.Transform;
    private readonly WorldBindings _world = NativeBindings.World;
    private readonly ComponentOpsBindings _componentOps = NativeBindings.ComponentOps;
    private readonly CameraBindings _camera = NativeBindings.Camera;
    private readonly ColliderBindings _collider = NativeBindings.Collider;
    private readonly ModelRendererBindings _modelRenderer = NativeBindings.ModelRenderer;
    private readonly LightRendererBindings _lightRenderer = NativeBindings.LightRenderer;
    private readonly PlayerControllerBindings _playerController = NativeBindings.PlayerController;

    internal Scope()
    {
      Calls.Clear();
      LastError = null;
      Canned.Clear();

      NativeBindings.InputUtils = MakeInputUtils();
      NativeBindings.RigidBody = MakeRigidBody();
      NativeBindings.Transform = MakeTransform();
      NativeBindings.World = MakeWorld();
      NativeBindings.ComponentOps = MakeComponentOps();
      NativeBindings.Camera = MakeCamera();
      NativeBindings.Collider = MakeCollider();
      NativeBindings.ModelRenderer = MakeModelRenderer();
      NativeBindings.LightRenderer = MakeLightRenderer();
      NativeBindings.PlayerController = MakePlayerController();
    }

    public void Dispose()
    {
      NativeBindings.InputUtils = _inputUtils;
      NativeBindings.RigidBody = _rigidBody;
      NativeBindings.Transform = _transform;
      NativeBindings.World = _world;
      NativeBindings.ComponentOps = _componentOps;
      NativeBindings.Camera = _camera;
      NativeBindings.Collider = _collider;
      NativeBindings.ModelRenderer = _modelRenderer;
      NativeBindings.LightRenderer = _lightRenderer;
      NativeBindings.PlayerController = _playerController;

      foreach (var ptr in Allocated)
      {
        Marshal.FreeCoTaskMem(ptr);
      }

      Allocated.Clear();
      Canned.Clear();
      Calls.Clear();
    }
  }

  private static T Get<T>(string key)
  {
    if (!Canned.TryGetValue(key, out var value))
    {
      return default!;
    }

    if (value is T typed)
    {
      return typed;
    }

    LastError ??= $"{key}: canned {value.GetType().Name}, expected {typeof(T).Name}";
    return default!;
  }

  private static byte Flag(string key) => Get<bool>(key) ? (byte)1 : (byte)0;

  private static string Text(object arg) => arg switch
  {
    IntPtr ptr => Marshal.PtrToStringUTF8(ptr) ?? "null",
    IFormattable formattable => formattable.ToString(null, CultureInfo.InvariantCulture),
    _ => arg.ToString() ?? ""
  };

  private static void Rec(string name, params object[] args) =>
    Calls.Add($"{name}({string.Join(",", Array.ConvertAll(args, Text))})");

  private static void OutVec3(string key, float* x, float* y, float* z)
  {
    var v = Get<Vector3>(key);
    *x = v.X;
    *y = v.Y;
    *z = v.Z;
  }

  private static void OutVec2(string key, float* x, float* y)
  {
    var v = Get<Vector2>(key);
    *x = v.X;
    *y = v.Y;
  }
}
