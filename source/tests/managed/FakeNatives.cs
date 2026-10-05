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
// tables are filled through a cast to the bool signature, which has the same one-byte shape.
internal static unsafe class FakeNatives
{
  internal static readonly List<string> Calls = new();

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

  private static T Get<T>(string key) => Canned.TryGetValue(key, out var value) ? (T)value : default!;

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

  // ---- Transform ----

  [UnmanagedCallersOnly]
  private static void TransformGetPosition(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getPosition", u);
    OutVec3("Transform.getPosition", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformGetScale(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getScale", u);
    OutVec3("Transform.getScale", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformGetRotation(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getRotation", u);
    OutVec3("Transform.getRotation", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformSetScale(IntPtr u, float x, float y, float z) =>
    Rec("Transform.setScale", u, x, y, z);

  [UnmanagedCallersOnly]
  private static void TransformSetRotation(IntPtr u, float x, float y, float z) =>
    Rec("Transform.setRotation", u, x, y, z);

  [UnmanagedCallersOnly]
  private static void TransformMove(IntPtr u, float x, float y, float z) => Rec("Transform.move", u, x, y, z);

  [UnmanagedCallersOnly]
  private static void TransformStart(IntPtr u) => Rec("Transform.start", u);

  [UnmanagedCallersOnly]
  private static void TransformStop(IntPtr u) => Rec("Transform.stop", u);

  [UnmanagedCallersOnly]
  private static byte TransformHas(IntPtr u)
  {
    Rec("Transform.has", u);
    return Flag("Transform.has");
  }

  [UnmanagedCallersOnly]
  private static void TransformGetLocalPosition(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getLocalPosition", u);
    OutVec3("Transform.getLocalPosition", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformGetLocalScale(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getLocalScale", u);
    OutVec3("Transform.getLocalScale", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformGetLocalRotation(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Transform.getLocalRotation", u);
    OutVec3("Transform.getLocalRotation", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void TransformSetPosition(IntPtr u, float x, float y, float z) =>
    Rec("Transform.setPosition", u, x, y, z);

  private static TransformBindings MakeTransform() => new()
  {
    getPosition = &TransformGetPosition,
    getScale = &TransformGetScale,
    getRotation = &TransformGetRotation,
    setScale = &TransformSetScale,
    setRotation = &TransformSetRotation,
    move = &TransformMove,
    start = &TransformStart,
    stop = &TransformStop,
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&TransformHas,
    getLocalPosition = &TransformGetLocalPosition,
    getLocalScale = &TransformGetLocalScale,
    getLocalRotation = &TransformGetLocalRotation,
    setPosition = &TransformSetPosition
  };

  // ---- RigidBody ----

  [UnmanagedCallersOnly]
  private static void RigidBodyApplyForce(IntPtr u, float x, float y, float z, float px, float py, float pz,
                                          int mode) =>
    Rec("RigidBody.applyForce", u, x, y, z, px, py, pz, mode);

  [UnmanagedCallersOnly]
  private static void RigidBodySetVelocity(IntPtr u, float x, float y, float z) =>
    Rec("RigidBody.setVelocity", u, x, y, z);

  [UnmanagedCallersOnly]
  private static byte RigidBodyIsFalling(IntPtr u)
  {
    Rec("RigidBody.isFalling", u);
    return Flag("RigidBody.isFalling");
  }

  [UnmanagedCallersOnly]
  private static byte RigidBodyHas(IntPtr u)
  {
    Rec("RigidBody.has", u);
    return Flag("RigidBody.has");
  }

  [UnmanagedCallersOnly]
  private static void RigidBodySetAngularVelocity(IntPtr u, float x, float y, float z) =>
    Rec("RigidBody.setAngularVelocity", u, x, y, z);

  [UnmanagedCallersOnly]
  private static void RigidBodyGetVelocity(IntPtr u, float* x, float* y, float* z)
  {
    Rec("RigidBody.getVelocity", u);
    OutVec3("RigidBody.getVelocity", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static void RigidBodyGetAngularVelocity(IntPtr u, float* x, float* y, float* z)
  {
    Rec("RigidBody.getAngularVelocity", u);
    OutVec3("RigidBody.getAngularVelocity", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static float RigidBodyGetMass(IntPtr u)
  {
    Rec("RigidBody.getMass", u);
    return Get<float>("RigidBody.getMass");
  }

  [UnmanagedCallersOnly]
  private static void RigidBodySetMass(IntPtr u, float value) => Rec("RigidBody.setMass", u, value);

  [UnmanagedCallersOnly]
  private static float RigidBodyGetFriction(IntPtr u)
  {
    Rec("RigidBody.getFriction", u);
    return Get<float>("RigidBody.getFriction");
  }

  [UnmanagedCallersOnly]
  private static void RigidBodySetFriction(IntPtr u, float value) => Rec("RigidBody.setFriction", u, value);

  [UnmanagedCallersOnly]
  private static float RigidBodyGetGravity(IntPtr u)
  {
    Rec("RigidBody.getGravity", u);
    return Get<float>("RigidBody.getGravity");
  }

  [UnmanagedCallersOnly]
  private static void RigidBodySetGravity(IntPtr u, float value) => Rec("RigidBody.setGravity", u, value);

  [UnmanagedCallersOnly]
  private static byte RigidBodyGetDoGravity(IntPtr u)
  {
    Rec("RigidBody.getDoGravity", u);
    return Flag("RigidBody.getDoGravity");
  }

  [UnmanagedCallersOnly]
  private static void RigidBodySetDoGravity(IntPtr u, byte value) => Rec("RigidBody.setDoGravity", u, value);

  private static RigidBodyBindings MakeRigidBody() => new()
  {
    applyForce = &RigidBodyApplyForce,
    setVelocity = &RigidBodySetVelocity,
    isFalling = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&RigidBodyIsFalling,
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&RigidBodyHas,
    setAngularVelocity = &RigidBodySetAngularVelocity,
    getVelocity = &RigidBodyGetVelocity,
    getAngularVelocity = &RigidBodyGetAngularVelocity,
    getMass = &RigidBodyGetMass,
    setMass = &RigidBodySetMass,
    getFriction = &RigidBodyGetFriction,
    setFriction = &RigidBodySetFriction,
    getGravity = &RigidBodyGetGravity,
    setGravity = &RigidBodySetGravity,
    getDoGravity =
      (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&RigidBodyGetDoGravity,
    setDoGravity =
      (delegate* unmanaged<IntPtr, bool, void>)(void*)(delegate* unmanaged<IntPtr, byte, void>)&RigidBodySetDoGravity
  };

  // ---- Camera ----

  [UnmanagedCallersOnly]
  private static void CameraGetDirection(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Camera.getDirection", u);
    OutVec3("Camera.getDirection", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static byte CameraHas(IntPtr u)
  {
    Rec("Camera.has", u);
    return Flag("Camera.has");
  }

  [UnmanagedCallersOnly]
  private static void CameraSetDirection(IntPtr u, float x, float y, float z) =>
    Rec("Camera.setDirection", u, x, y, z);

  [UnmanagedCallersOnly]
  private static float CameraGetFov(IntPtr u)
  {
    Rec("Camera.getFov", u);
    return Get<float>("Camera.getFov");
  }

  [UnmanagedCallersOnly]
  private static void CameraSetFov(IntPtr u, float value) => Rec("Camera.setFov", u, value);

  [UnmanagedCallersOnly]
  private static float CameraGetNearPlane(IntPtr u)
  {
    Rec("Camera.getNearPlane", u);
    return Get<float>("Camera.getNearPlane");
  }

  [UnmanagedCallersOnly]
  private static void CameraSetNearPlane(IntPtr u, float value) => Rec("Camera.setNearPlane", u, value);

  [UnmanagedCallersOnly]
  private static float CameraGetFarPlane(IntPtr u)
  {
    Rec("Camera.getFarPlane", u);
    return Get<float>("Camera.getFarPlane");
  }

  [UnmanagedCallersOnly]
  private static void CameraSetFarPlane(IntPtr u, float value) => Rec("Camera.setFarPlane", u, value);

  [UnmanagedCallersOnly]
  private static byte CameraIsActive(IntPtr u)
  {
    Rec("Camera.isActive", u);
    return Flag("Camera.isActive");
  }

  [UnmanagedCallersOnly]
  private static void CameraSetActive(IntPtr u, byte value) => Rec("Camera.setActive", u, value);

  private static CameraBindings MakeCamera() => new()
  {
    getDirection = &CameraGetDirection,
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&CameraHas,
    setDirection = &CameraSetDirection,
    getFov = &CameraGetFov,
    setFov = &CameraSetFov,
    getNearPlane = &CameraGetNearPlane,
    setNearPlane = &CameraSetNearPlane,
    getFarPlane = &CameraGetFarPlane,
    setFarPlane = &CameraSetFarPlane,
    isActive = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&CameraIsActive,
    setActive =
      (delegate* unmanaged<IntPtr, bool, void>)(void*)(delegate* unmanaged<IntPtr, byte, void>)&CameraSetActive
  };

  // ---- Collider ----

  [UnmanagedCallersOnly]
  private static int ColliderGetShape(IntPtr u)
  {
    Rec("Collider.getShape", u);
    return Get<int>("Collider.getShape");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderGetIsTrigger(IntPtr u)
  {
    Rec("Collider.getIsTrigger", u);
    return Flag("Collider.getIsTrigger");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetIsTrigger(IntPtr u, byte value)
  {
    Rec("Collider.setIsTrigger", u, value);
    return Flag("Collider.setIsTrigger");
  }

  [UnmanagedCallersOnly]
  private static uint ColliderGetLayer(IntPtr u)
  {
    Rec("Collider.getLayer", u);
    return Get<uint>("Collider.getLayer");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetLayer(IntPtr u, uint value)
  {
    Rec("Collider.setLayer", u, value);
    return Flag("Collider.setLayer");
  }

  [UnmanagedCallersOnly]
  private static uint ColliderGetMask(IntPtr u)
  {
    Rec("Collider.getMask", u);
    return Get<uint>("Collider.getMask");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetMask(IntPtr u, uint value)
  {
    Rec("Collider.setMask", u, value);
    return Flag("Collider.setMask");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderGetBoxOffset(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Collider.getBoxOffset", u);
    OutVec3("Collider.getBoxOffset", x, y, z);
    return Flag("Collider.getBoxOffset.ok");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetBoxOffset(IntPtr u, float x, float y, float z)
  {
    Rec("Collider.setBoxOffset", u, x, y, z);
    return Flag("Collider.setBoxOffset");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderGetBoxSize(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Collider.getBoxSize", u);
    OutVec3("Collider.getBoxSize", x, y, z);
    return Flag("Collider.getBoxSize.ok");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetBoxSize(IntPtr u, float x, float y, float z)
  {
    Rec("Collider.setBoxSize", u, x, y, z);
    return Flag("Collider.setBoxSize");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderGetSphereOffset(IntPtr u, float* x, float* y, float* z)
  {
    Rec("Collider.getSphereOffset", u);
    OutVec3("Collider.getSphereOffset", x, y, z);
    return Flag("Collider.getSphereOffset.ok");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetSphereOffset(IntPtr u, float x, float y, float z)
  {
    Rec("Collider.setSphereOffset", u, x, y, z);
    return Flag("Collider.setSphereOffset");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderGetSphereRadius(IntPtr u, float* r)
  {
    Rec("Collider.getSphereRadius", u);
    *r = Get<float>("Collider.getSphereRadius");
    return Flag("Collider.getSphereRadius.ok");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderSetSphereRadius(IntPtr u, float value)
  {
    Rec("Collider.setSphereRadius", u, value);
    return Flag("Collider.setSphereRadius");
  }

  [UnmanagedCallersOnly]
  private static byte ColliderHas(IntPtr u)
  {
    Rec("Collider.has", u);
    return Flag("Collider.has");
  }

  private static ColliderBindings MakeCollider() => new()
  {
    getShape = &ColliderGetShape,
    getIsTrigger =
      (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&ColliderGetIsTrigger,
    setIsTrigger =
      (delegate* unmanaged<IntPtr, bool, bool>)(void*)(delegate* unmanaged<IntPtr, byte, byte>)&ColliderSetIsTrigger,
    getLayer = &ColliderGetLayer,
    setLayer = (delegate* unmanaged<IntPtr, uint, bool>)(void*)(delegate* unmanaged<IntPtr, uint, byte>)&ColliderSetLayer,
    getMask = &ColliderGetMask,
    setMask = (delegate* unmanaged<IntPtr, uint, bool>)(void*)(delegate* unmanaged<IntPtr, uint, byte>)&ColliderSetMask,
    getBoxOffset = (delegate* unmanaged<IntPtr, float*, float*, float*, bool>)(void*)
      (delegate* unmanaged<IntPtr, float*, float*, float*, byte>)&ColliderGetBoxOffset,
    setBoxOffset = (delegate* unmanaged<IntPtr, float, float, float, bool>)(void*)
      (delegate* unmanaged<IntPtr, float, float, float, byte>)&ColliderSetBoxOffset,
    getBoxSize = (delegate* unmanaged<IntPtr, float*, float*, float*, bool>)(void*)
      (delegate* unmanaged<IntPtr, float*, float*, float*, byte>)&ColliderGetBoxSize,
    setBoxSize = (delegate* unmanaged<IntPtr, float, float, float, bool>)(void*)
      (delegate* unmanaged<IntPtr, float, float, float, byte>)&ColliderSetBoxSize,
    getSphereOffset = (delegate* unmanaged<IntPtr, float*, float*, float*, bool>)(void*)
      (delegate* unmanaged<IntPtr, float*, float*, float*, byte>)&ColliderGetSphereOffset,
    setSphereOffset = (delegate* unmanaged<IntPtr, float, float, float, bool>)(void*)
      (delegate* unmanaged<IntPtr, float, float, float, byte>)&ColliderSetSphereOffset,
    getSphereRadius = (delegate* unmanaged<IntPtr, float*, bool>)(void*)
      (delegate* unmanaged<IntPtr, float*, byte>)&ColliderGetSphereRadius,
    setSphereRadius = (delegate* unmanaged<IntPtr, float, bool>)(void*)
      (delegate* unmanaged<IntPtr, float, byte>)&ColliderSetSphereRadius,
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&ColliderHas
  };

  // ---- InputUtils ----

  [UnmanagedCallersOnly]
  private static byte InputKeyIsPressed(int key)
  {
    Rec("InputUtils.keyIsPressed", key);
    return Flag("InputUtils.keyIsPressed");
  }

  [UnmanagedCallersOnly]
  private static byte InputWindowIsFocused()
  {
    Rec("InputUtils.windowIsFocused");
    return Flag("InputUtils.windowIsFocused");
  }

  [UnmanagedCallersOnly]
  private static byte InputKeyIsPressedForObject(IntPtr u, int key)
  {
    Rec("InputUtils.keyIsPressedForObject", u, key);
    return Flag("InputUtils.keyIsPressedForObject");
  }

  [UnmanagedCallersOnly]
  private static byte InputWindowIsFocusedForObject(IntPtr u)
  {
    Rec("InputUtils.windowIsFocusedForObject", u);
    return Flag("InputUtils.windowIsFocusedForObject");
  }

  [UnmanagedCallersOnly]
  private static void InputMousePositionForObject(IntPtr u, float* x, float* y)
  {
    Rec("InputUtils.mousePositionForObject", u);
    OutVec2("InputUtils.mousePositionForObject", x, y);
  }

  [UnmanagedCallersOnly]
  private static void InputMouseDeltaForObject(IntPtr u, float* x, float* y)
  {
    Rec("InputUtils.mouseDeltaForObject", u);
    OutVec2("InputUtils.mouseDeltaForObject", x, y);
  }

  [UnmanagedCallersOnly]
  private static float InputScrollForObject(IntPtr u)
  {
    Rec("InputUtils.scrollForObject", u);
    return Get<float>("InputUtils.scrollForObject");
  }

  [UnmanagedCallersOnly]
  private static byte InputMouseButtonForObject(IntPtr u, int button)
  {
    Rec("InputUtils.mouseButtonForObject", u, button);
    return Flag("InputUtils.mouseButtonForObject");
  }

  [UnmanagedCallersOnly]
  private static byte InputWasKeyPressedThisTickForObject(IntPtr u, int key)
  {
    Rec("InputUtils.wasKeyPressedThisTickForObject", u, key);
    return Flag("InputUtils.wasKeyPressedThisTickForObject");
  }

  [UnmanagedCallersOnly]
  private static byte InputWasKeyReleasedThisTickForObject(IntPtr u, int key)
  {
    Rec("InputUtils.wasKeyReleasedThisTickForObject", u, key);
    return Flag("InputUtils.wasKeyReleasedThisTickForObject");
  }

  private static InputUtilsBindings MakeInputUtils() => new()
  {
    keyIsPressed = (delegate* unmanaged<int, bool>)(void*)(delegate* unmanaged<int, byte>)&InputKeyIsPressed,
    windowIsFocused = (delegate* unmanaged<bool>)(void*)(delegate* unmanaged<byte>)&InputWindowIsFocused,
    keyIsPressedForObject = (delegate* unmanaged<IntPtr, int, bool>)(void*)
      (delegate* unmanaged<IntPtr, int, byte>)&InputKeyIsPressedForObject,
    windowIsFocusedForObject = (delegate* unmanaged<IntPtr, bool>)(void*)
      (delegate* unmanaged<IntPtr, byte>)&InputWindowIsFocusedForObject,
    mousePositionForObject = &InputMousePositionForObject,
    mouseDeltaForObject = &InputMouseDeltaForObject,
    scrollForObject = &InputScrollForObject,
    mouseButtonForObject = (delegate* unmanaged<IntPtr, int, bool>)(void*)
      (delegate* unmanaged<IntPtr, int, byte>)&InputMouseButtonForObject,
    wasKeyPressedThisTickForObject = (delegate* unmanaged<IntPtr, int, bool>)(void*)
      (delegate* unmanaged<IntPtr, int, byte>)&InputWasKeyPressedThisTickForObject,
    wasKeyReleasedThisTickForObject = (delegate* unmanaged<IntPtr, int, bool>)(void*)
      (delegate* unmanaged<IntPtr, int, byte>)&InputWasKeyReleasedThisTickForObject
  };

  // ---- ModelRenderer ----

  [UnmanagedCallersOnly]
  private static IntPtr ModelGetModelUuid(IntPtr u)
  {
    Rec("ModelRenderer.getModelUUID", u);
    return Get<IntPtr>("ModelRenderer.getModelUUID");
  }

  [UnmanagedCallersOnly]
  private static IntPtr ModelGetTextureUuid(IntPtr u)
  {
    Rec("ModelRenderer.getTextureUUID", u);
    return Get<IntPtr>("ModelRenderer.getTextureUUID");
  }

  [UnmanagedCallersOnly]
  private static byte ModelGetShouldRender(IntPtr u)
  {
    Rec("ModelRenderer.getShouldRender", u);
    return Flag("ModelRenderer.getShouldRender");
  }

  [UnmanagedCallersOnly]
  private static byte ModelSetModelUuid(IntPtr u, IntPtr asset)
  {
    Rec("ModelRenderer.setModelUUID", u, asset);
    return Flag("ModelRenderer.setModelUUID");
  }

  [UnmanagedCallersOnly]
  private static byte ModelSetTextureUuid(IntPtr u, IntPtr asset)
  {
    Rec("ModelRenderer.setTextureUUID", u, asset);
    return Flag("ModelRenderer.setTextureUUID");
  }

  [UnmanagedCallersOnly]
  private static void ModelSetShouldRender(IntPtr u, byte value) => Rec("ModelRenderer.setShouldRender", u, value);

  [UnmanagedCallersOnly]
  private static byte ModelHas(IntPtr u)
  {
    Rec("ModelRenderer.has", u);
    return Flag("ModelRenderer.has");
  }

  private static ModelRendererBindings MakeModelRenderer() => new()
  {
    getModelUUID = &ModelGetModelUuid,
    getTextureUUID = &ModelGetTextureUuid,
    getShouldRender =
      (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&ModelGetShouldRender,
    setModelUUID = (delegate* unmanaged<IntPtr, IntPtr, bool>)(void*)
      (delegate* unmanaged<IntPtr, IntPtr, byte>)&ModelSetModelUuid,
    setTextureUUID = (delegate* unmanaged<IntPtr, IntPtr, bool>)(void*)
      (delegate* unmanaged<IntPtr, IntPtr, byte>)&ModelSetTextureUuid,
    setShouldRender = (delegate* unmanaged<IntPtr, bool, void>)(void*)
      (delegate* unmanaged<IntPtr, byte, void>)&ModelSetShouldRender,
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&ModelHas
  };

  // ---- LightRenderer ----

  [UnmanagedCallersOnly]
  private static byte LightGetIsSpotLight(IntPtr u)
  {
    Rec("LightRenderer.getIsSpotLight", u);
    return Flag("LightRenderer.getIsSpotLight");
  }

  [UnmanagedCallersOnly]
  private static void LightGetColor(IntPtr u, float* r, float* g, float* b)
  {
    Rec("LightRenderer.getColor", u);
    OutVec3("LightRenderer.getColor", r, g, b);
  }

  [UnmanagedCallersOnly]
  private static float LightGetAmbient(IntPtr u)
  {
    Rec("LightRenderer.getAmbient", u);
    return Get<float>("LightRenderer.getAmbient");
  }

  [UnmanagedCallersOnly]
  private static float LightGetDiffuse(IntPtr u)
  {
    Rec("LightRenderer.getDiffuse", u);
    return Get<float>("LightRenderer.getDiffuse");
  }

  [UnmanagedCallersOnly]
  private static float LightGetSpecular(IntPtr u)
  {
    Rec("LightRenderer.getSpecular", u);
    return Get<float>("LightRenderer.getSpecular");
  }

  [UnmanagedCallersOnly]
  private static void LightGetDirection(IntPtr u, float* x, float* y, float* z)
  {
    Rec("LightRenderer.getDirection", u);
    OutVec3("LightRenderer.getDirection", x, y, z);
  }

  [UnmanagedCallersOnly]
  private static float LightGetConeAngle(IntPtr u)
  {
    Rec("LightRenderer.getConeAngle", u);
    return Get<float>("LightRenderer.getConeAngle");
  }

  [UnmanagedCallersOnly]
  private static void LightSetSpotLight(IntPtr u, byte value) => Rec("LightRenderer.setSpotLight", u, value);

  [UnmanagedCallersOnly]
  private static void LightSetColor(IntPtr u, float r, float g, float b) =>
    Rec("LightRenderer.setColor", u, r, g, b);

  [UnmanagedCallersOnly]
  private static void LightSetAmbient(IntPtr u, float value) => Rec("LightRenderer.setAmbient", u, value);

  [UnmanagedCallersOnly]
  private static void LightSetDiffuse(IntPtr u, float value) => Rec("LightRenderer.setDiffuse", u, value);

  [UnmanagedCallersOnly]
  private static void LightSetSpecular(IntPtr u, float value) => Rec("LightRenderer.setSpecular", u, value);

  [UnmanagedCallersOnly]
  private static void LightSetDirection(IntPtr u, float x, float y, float z) =>
    Rec("LightRenderer.setDirection", u, x, y, z);

  [UnmanagedCallersOnly]
  private static void LightSetConeAngle(IntPtr u, float value) => Rec("LightRenderer.setConeAngle", u, value);

  [UnmanagedCallersOnly]
  private static byte LightHas(IntPtr u)
  {
    Rec("LightRenderer.has", u);
    return Flag("LightRenderer.has");
  }

  private static LightRendererBindings MakeLightRenderer() => new()
  {
    getIsSpotLight =
      (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&LightGetIsSpotLight,
    getColor = &LightGetColor,
    getAmbient = &LightGetAmbient,
    getDiffuse = &LightGetDiffuse,
    getSpecular = &LightGetSpecular,
    getDirection = &LightGetDirection,
    getConeAngle = &LightGetConeAngle,
    setSpotLight = (delegate* unmanaged<IntPtr, bool, void>)(void*)
      (delegate* unmanaged<IntPtr, byte, void>)&LightSetSpotLight,
    setColor = &LightSetColor,
    setAmbient = &LightSetAmbient,
    setDiffuse = &LightSetDiffuse,
    setSpecular = &LightSetSpecular,
    setDirection = &LightSetDirection,
    setConeAngle = &LightSetConeAngle,
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&LightHas
  };

  // ---- PlayerController ----

  [UnmanagedCallersOnly]
  private static byte PlayerHas(IntPtr u)
  {
    Rec("PlayerController.has", u);
    return Flag("PlayerController.has");
  }

  [UnmanagedCallersOnly]
  private static int PlayerGetSlot(IntPtr u)
  {
    Rec("PlayerController.getPlayerSlot", u);
    return Get<int>("PlayerController.getPlayerSlot");
  }

  [UnmanagedCallersOnly]
  private static void PlayerSetSlot(IntPtr u, int slot) => Rec("PlayerController.setPlayerSlot", u, slot);

  private static PlayerControllerBindings MakePlayerController() => new()
  {
    has = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&PlayerHas,
    getPlayerSlot = &PlayerGetSlot,
    setPlayerSlot = &PlayerSetSlot
  };

  // ---- World ----

  [UnmanagedCallersOnly]
  private static IntPtr WorldFindObjectByName(IntPtr name)
  {
    Rec("World.findObjectByName", name);
    return Get<IntPtr>("World.findObjectByName");
  }

  [UnmanagedCallersOnly]
  private static IntPtr WorldGetObjectName(IntPtr u)
  {
    Rec("World.getObjectName", u);
    return Get<IntPtr>("World.getObjectName");
  }

  [UnmanagedCallersOnly]
  private static byte WorldObjectExists(IntPtr u)
  {
    Rec("World.objectExists", u);
    return Flag("World.objectExists");
  }

  [UnmanagedCallersOnly]
  private static IntPtr WorldGetAllObjectUuids()
  {
    Rec("World.getAllObjectUuids");
    return Get<IntPtr>("World.getAllObjectUuids");
  }

  [UnmanagedCallersOnly]
  private static IntPtr WorldSpawnObject(IntPtr name, float x, float y, float z)
  {
    Rec("World.spawnObject", name, x, y, z);
    return Get<IntPtr>("World.spawnObject");
  }

  [UnmanagedCallersOnly]
  private static void WorldDestroyObject(IntPtr u) => Rec("World.destroyObject", u);

  [UnmanagedCallersOnly]
  private static IntPtr WorldRaycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance,
                                     uint layerMask, IntPtr ignore)
  {
    Rec("World.raycast", ox, oy, oz, dx, dy, dz, maxDistance, layerMask, ignore);
    return Get<IntPtr>("World.raycast");
  }

  [UnmanagedCallersOnly]
  private static IntPtr WorldOverlapSphere(float x, float y, float z, float radius, uint layerMask, IntPtr ignore)
  {
    Rec("World.overlapSphere", x, y, z, radius, layerMask, ignore);
    return Get<IntPtr>("World.overlapSphere");
  }

  [UnmanagedCallersOnly]
  private static IntPtr WorldSpawnPrefab(IntPtr prefab, float x, float y, float z)
  {
    Rec("World.spawnPrefab", prefab, x, y, z);
    return Get<IntPtr>("World.spawnPrefab");
  }

  private static WorldBindings MakeWorld() => new()
  {
    findObjectByName = &WorldFindObjectByName,
    getObjectName = &WorldGetObjectName,
    objectExists = (delegate* unmanaged<IntPtr, bool>)(void*)(delegate* unmanaged<IntPtr, byte>)&WorldObjectExists,
    getAllObjectUuids = &WorldGetAllObjectUuids,
    spawnObject = &WorldSpawnObject,
    destroyObject = &WorldDestroyObject,
    raycast = &WorldRaycast,
    overlapSphere = &WorldOverlapSphere,
    spawnPrefab = &WorldSpawnPrefab
  };

  // ---- ComponentOps ----

  [UnmanagedCallersOnly]
  private static byte OpsHasComponent(IntPtr u, IntPtr type)
  {
    Rec("ComponentOps.hasComponent", u, type);
    return Flag("ComponentOps.hasComponent");
  }

  [UnmanagedCallersOnly]
  private static byte OpsAddComponent(IntPtr u, IntPtr type)
  {
    Rec("ComponentOps.addComponent", u, type);
    return Flag("ComponentOps.addComponent");
  }

  [UnmanagedCallersOnly]
  private static byte OpsRemoveComponent(IntPtr u, IntPtr type)
  {
    Rec("ComponentOps.removeComponent", u, type);
    return Flag("ComponentOps.removeComponent");
  }

  [UnmanagedCallersOnly]
  private static IntPtr OpsGetComponentTypes(IntPtr u)
  {
    Rec("ComponentOps.getComponentTypes", u);
    return Get<IntPtr>("ComponentOps.getComponentTypes");
  }

  private static ComponentOpsBindings MakeComponentOps() => new()
  {
    hasComponent = (delegate* unmanaged<IntPtr, IntPtr, bool>)(void*)
      (delegate* unmanaged<IntPtr, IntPtr, byte>)&OpsHasComponent,
    addComponent = (delegate* unmanaged<IntPtr, IntPtr, bool>)(void*)
      (delegate* unmanaged<IntPtr, IntPtr, byte>)&OpsAddComponent,
    removeComponent = (delegate* unmanaged<IntPtr, IntPtr, bool>)(void*)
      (delegate* unmanaged<IntPtr, IntPtr, byte>)&OpsRemoveComponent,
    getComponentTypes = &OpsGetComponentTypes
  };
}
