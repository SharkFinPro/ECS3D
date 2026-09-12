using System;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct ModelRendererBindings
{
    public delegate* unmanaged<IntPtr, IntPtr> getModelUUID;
    public delegate* unmanaged<IntPtr, IntPtr> getTextureUUID;
    public delegate* unmanaged<IntPtr, bool> getShouldRender;
    public delegate* unmanaged<IntPtr, IntPtr, bool> setModelUUID;
    public delegate* unmanaged<IntPtr, IntPtr, bool> setTextureUUID;
    public delegate* unmanaged<IntPtr, bool, void> setShouldRender;
    public delegate* unmanaged<IntPtr, bool> has;
}

// What an object looks like: its model/texture asset and whether it renders at all. Reached via
// World.tryGetModelRenderer like Transform/RigidBody, since a script typically changes another object's
// appearance rather than its own.
//
// setModel/setTexture take an asset uuid (as shown by the editor's asset browser) and return false without
// changing anything when it doesn't name a registered asset of the right type - swapping in a texture uuid
// for the model (or vice versa) fails safely instead of corrupting the renderer.
public unsafe class ModelRenderer
{
    private readonly IntPtr _uuid;

    internal ModelRenderer(string uuid)
    {
        _uuid = Marshal.StringToCoTaskMemUTF8(uuid);
    }

    ~ModelRenderer()
    {
        Marshal.FreeCoTaskMem(_uuid);
    }

    public string getModelUUID() =>
        Marshal.PtrToStringUTF8(NativeBindings.ModelRenderer.getModelUUID(_uuid)) ?? "";

    public string getTextureUUID() =>
        Marshal.PtrToStringUTF8(NativeBindings.ModelRenderer.getTextureUUID(_uuid)) ?? "";

    public bool getShouldRender() => NativeBindings.ModelRenderer.getShouldRender(_uuid);

    public bool setModel(string modelUUID)
    {
        var assetPtr = Marshal.StringToCoTaskMemUTF8(modelUUID);
        try
        {
            return NativeBindings.ModelRenderer.setModelUUID(_uuid, assetPtr);
        }
        finally
        {
            Marshal.FreeCoTaskMem(assetPtr);
        }
    }

    public bool setTexture(string textureUUID)
    {
        var assetPtr = Marshal.StringToCoTaskMemUTF8(textureUUID);
        try
        {
            return NativeBindings.ModelRenderer.setTextureUUID(_uuid, assetPtr);
        }
        finally
        {
            Marshal.FreeCoTaskMem(assetPtr);
        }
    }

    public void setShouldRender(bool shouldRender) =>
        NativeBindings.ModelRenderer.setShouldRender(_uuid, shouldRender);
}
