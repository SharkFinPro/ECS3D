using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;

namespace ECS3DManagedTests;

internal static unsafe partial class FakeNatives
{
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
}
