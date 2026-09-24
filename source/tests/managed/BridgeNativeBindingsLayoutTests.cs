using System;
using System.Reflection;
using System.Runtime.InteropServices;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Each *Bindings struct (TransformBindings, RigidBodyBindings, ...) is a [StructLayout(Sequential)] block
// of native function pointers, mirrored field-for-field by a native C++ struct of the same name - see the
// "add new fields at the END" comments beside each one. A stray non-pointer-sized field, or one inserted
// out of order under a build that widens/narrows it, would silently misalign every field after it against
// the native side instead of failing to compile. These tests catch that class of bug the same way for
// every provider: a Bindings struct's total size must equal (field count * pointer size), which only holds
// if every field is itself a single pointer-sized value (a delegate* unmanaged<...>).
public unsafe class BridgeNativeBindingsLayoutTests
{
  private static void AssertAllFieldsArePointerSized<T>() where T : unmanaged
  {
    var fieldCount = typeof(T).GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).Length;

    Assert.Equal(fieldCount * IntPtr.Size, sizeof(T));
  }

  [Fact]
  public void TransformBindings_IsEntirelyFunctionPointers() => AssertAllFieldsArePointerSized<TransformBindings>();

  [Fact]
  public void RigidBodyBindings_IsEntirelyFunctionPointers() => AssertAllFieldsArePointerSized<RigidBodyBindings>();

  [Fact]
  public void CameraBindings_IsEntirelyFunctionPointers() => AssertAllFieldsArePointerSized<CameraBindings>();

  [Fact]
  public void InputUtilsBindings_IsEntirelyFunctionPointers() => AssertAllFieldsArePointerSized<InputUtilsBindings>();

  [Fact]
  public void LogBindings_IsEntirelyFunctionPointers() => AssertAllFieldsArePointerSized<LogBindings>();

  [Fact]
  public void WorldBindings_IsEntirelyFunctionPointers() => AssertAllFieldsArePointerSized<WorldBindings>();

  [Fact]
  public void ComponentOpsBindings_IsEntirelyFunctionPointers() => AssertAllFieldsArePointerSized<ComponentOpsBindings>();

  [Fact]
  public void ColliderBindings_IsEntirelyFunctionPointers() => AssertAllFieldsArePointerSized<ColliderBindings>();

  [Fact]
  public void ModelRendererBindings_IsEntirelyFunctionPointers() => AssertAllFieldsArePointerSized<ModelRendererBindings>();

  [Fact]
  public void LightRendererBindings_IsEntirelyFunctionPointers() => AssertAllFieldsArePointerSized<LightRendererBindings>();

  // Positive control for every test above: a struct with deliberately non-pointer-sized fields (bytes
  // mixed in with function pointers, the exact mistake these tests exist to catch) must fail the same
  // assertion instead of it passing vacuously for any struct. Two stray byte fields (rather than one) so
  // the mismatch survives trailing alignment padding: one stray byte alone pads out to the same total size
  // a second pointer field would have taken, which would make this "bad" struct pass by coincidence.
  [StructLayout(LayoutKind.Sequential)]
  private unsafe struct BadBindingsWithStrayFields
  {
    public delegate* unmanaged<IntPtr, bool> has;
    public byte strayFieldOne;
    public byte strayFieldTwo;
  }

  [Fact]
  public void AssertAllFieldsArePointerSized_FailsWhenStrayFieldsArePresent()
  {
    var fieldCount = typeof(BadBindingsWithStrayFields)
      .GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).Length;

    Assert.NotEqual(fieldCount * IntPtr.Size, sizeof(BadBindingsWithStrayFields));
  }
}
