using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ScriptBridge;
using static ECS3DManagedTests.FakeNatives;

namespace ECS3DManagedTests;

internal static unsafe class FakeModelRendererNatives
{
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

  internal static ModelRendererBindings Make() => new()
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
}
