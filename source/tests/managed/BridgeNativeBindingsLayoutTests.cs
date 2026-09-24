using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Each *Bindings struct (TransformBindings, RigidBodyBindings, ...) is a [StructLayout(Sequential)] block
// of native function pointers, mirrored field-for-field by a native C++ struct of the same name - see the
// "add new fields at the END" comments beside each one. A stray non-pointer-sized field, or one inserted
// out of order under a build that widens/narrows it, would silently misalign every field after it against
// the native side instead of failing to compile. This asserts that invariant - a Bindings struct's total
// size must equal (field count * pointer size), which only holds if every field is itself a single
// pointer-sized value (a delegate* unmanaged<...>) - for every struct in the ScriptBridge assembly whose
// name ends in "Bindings", discovered by reflection rather than a hardcoded list, so a newly added
// provider (e.g. a future PlayerControllerBindings) is covered automatically instead of needing this file
// edited too.
public unsafe class BridgeNativeBindingsLayoutTests
{
  // sizeof(T) (used below) needs T known as an unmanaged type at compile time, which a reflection-driven
  // type isn't; Invoke()ing a generic method built with MakeGenericMethod is what lets the same compiler-
  // checked sizeof(T) operator - rather than Marshal.SizeOf, whose blittability rules for delegate*
  // fields are not something this project can verify without building - run against a runtime Type.
  private static readonly MethodInfo SizeOfMethod =
    typeof(BridgeNativeBindingsLayoutTests)
      .GetMethod(nameof(SizeOfUnmanaged), BindingFlags.NonPublic | BindingFlags.Static)!;

  private static int SizeOfUnmanaged<T>() where T : unmanaged => sizeof(T);

  private static int SizeOf(Type unmanagedType) =>
    (int)SizeOfMethod.MakeGenericMethod(unmanagedType).Invoke(null, null)!;

  public static IEnumerable<object[]> BindingsStructs() =>
    typeof(Bridge).Assembly.GetTypes()
      .Where(t => t.IsValueType && !t.IsEnum && t.Name.EndsWith("Bindings", StringComparison.Ordinal))
      .Select(t => new object[] { t });

  [Theory]
  [MemberData(nameof(BindingsStructs))]
  public void BindingsStruct_IsEntirelyFunctionPointers(Type bindingsType)
  {
    var fieldCount = bindingsType
      .GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).Length;

    Assert.Equal(fieldCount * IntPtr.Size, SizeOf(bindingsType));
  }

  // Guards the Theory above against a broken or over-narrow discovery filter (e.g. a typo'd suffix, or
  // matching the wrong assembly): a Theory over zero MemberData cases still reports green, so this counts
  // the providers known at the time this test was written and fails loudly if that floor is not met.
  [Fact]
  public void BindingsStructs_FindsAtLeastTheKnownProviders()
  {
    var found = BindingsStructs().Count();

    Assert.True(found >= 10, $"Expected at least 10 *Bindings structs in ScriptBridge, found {found}.");
  }

  // Positive control for BindingsStruct_IsEntirelyFunctionPointers: a struct with deliberately non-
  // pointer-sized fields (bytes mixed in with function pointers, the exact mistake that test exists to
  // catch) must fail the same assertion instead of it passing vacuously for any struct. Two stray byte
  // fields (rather than one) so the mismatch survives trailing alignment padding: one stray byte alone
  // pads out to the same total size a second pointer field would have taken, which would make this "bad"
  // struct pass by coincidence. Named without a "Bindings" suffix, and declared in this test assembly
  // rather than ScriptBridge's, so BindingsStructs() above never picks it up as a real provider.
  [StructLayout(LayoutKind.Sequential)]
  private readonly struct BadBindingsWithStrayFields : IEquatable<BadBindingsWithStrayFields>
  {
    public readonly delegate* unmanaged<IntPtr, bool> has;
    public readonly byte strayFieldOne;
    public readonly byte strayFieldTwo;

    public bool Equals(BadBindingsWithStrayFields other) =>
      has == other.has && strayFieldOne == other.strayFieldOne && strayFieldTwo == other.strayFieldTwo;

    public override bool Equals(object? obj) => obj is BadBindingsWithStrayFields other && Equals(other);

    public override int GetHashCode() => HashCode.Combine((IntPtr)has, strayFieldOne, strayFieldTwo);
  }

  [Fact]
  public void BindingsStruct_IsEntirelyFunctionPointers_FailsWhenStrayFieldsArePresent()
  {
    var fieldCount = typeof(BadBindingsWithStrayFields)
      .GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).Length;

    Assert.NotEqual(fieldCount * IntPtr.Size, SizeOf(typeof(BadBindingsWithStrayFields)));
  }
}
