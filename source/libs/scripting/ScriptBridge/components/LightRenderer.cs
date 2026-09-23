using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace ScriptBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct LightRendererBindings
{
    public delegate* unmanaged<IntPtr, bool> getIsSpotLight;
    public delegate* unmanaged<IntPtr, float*, float*, float*, void> getColor;
    public delegate* unmanaged<IntPtr, float> getAmbient;
    public delegate* unmanaged<IntPtr, float> getDiffuse;
    public delegate* unmanaged<IntPtr, float> getSpecular;
    public delegate* unmanaged<IntPtr, float*, float*, float*, void> getDirection;
    public delegate* unmanaged<IntPtr, float> getConeAngle;
    public delegate* unmanaged<IntPtr, bool, void> setSpotLight;
    public delegate* unmanaged<IntPtr, float, float, float, void> setColor;
    public delegate* unmanaged<IntPtr, float, void> setAmbient;
    public delegate* unmanaged<IntPtr, float, void> setDiffuse;
    public delegate* unmanaged<IntPtr, float, void> setSpecular;
    public delegate* unmanaged<IntPtr, float, float, float, void> setDirection;
    public delegate* unmanaged<IntPtr, float, void> setConeAngle;
    public delegate* unmanaged<IntPtr, bool> has;
}

// How an object lights the scene: color, the ambient/diffuse/specular strengths, whether it's a spot
// light, and the spot direction/cone angle. Reached via World.tryGetLightRenderer like Transform/
// ModelRenderer, since a script typically changes another object's light rather than its own.
//
// Every setter goes through the native component's own setter, so its rules still apply: a non-finite
// color/strength/direction is ignored, and setConeAngle clamps to the component's configured range.
public unsafe class LightRenderer
{
    private readonly IntPtr _uuid;

    internal LightRenderer(string uuid)
    {
        _uuid = Marshal.StringToCoTaskMemUTF8(uuid);
    }

    ~LightRenderer()
    {
        Marshal.FreeCoTaskMem(_uuid);
    }

    public bool getIsSpotLight() => NativeBindings.LightRenderer.getIsSpotLight(_uuid);

    public Vector3 getColor()
    {
        float r = 0, g = 0, b = 0;
        NativeBindings.LightRenderer.getColor(_uuid, &r, &g, &b);

        return new Vector3(r, g, b);
    }

    public float getAmbient() => NativeBindings.LightRenderer.getAmbient(_uuid);

    public float getDiffuse() => NativeBindings.LightRenderer.getDiffuse(_uuid);

    public float getSpecular() => NativeBindings.LightRenderer.getSpecular(_uuid);

    public Vector3 getDirection()
    {
        float x = 0, y = 0, z = 0;
        NativeBindings.LightRenderer.getDirection(_uuid, &x, &y, &z);

        return new Vector3(x, y, z);
    }

    public float getConeAngle() => NativeBindings.LightRenderer.getConeAngle(_uuid);

    public void setSpotLight(bool isSpotLight) =>
        NativeBindings.LightRenderer.setSpotLight(_uuid, isSpotLight);

    public void setColor(float r, float g, float b) =>
        NativeBindings.LightRenderer.setColor(_uuid, r, g, b);

    public void setColor(Vector3 color) => setColor(color.X, color.Y, color.Z);

    public void setAmbient(float ambient) => NativeBindings.LightRenderer.setAmbient(_uuid, ambient);

    public void setDiffuse(float diffuse) => NativeBindings.LightRenderer.setDiffuse(_uuid, diffuse);

    public void setSpecular(float specular) => NativeBindings.LightRenderer.setSpecular(_uuid, specular);

    public void setDirection(float x, float y, float z) =>
        NativeBindings.LightRenderer.setDirection(_uuid, x, y, z);

    public void setDirection(Vector3 direction) => setDirection(direction.X, direction.Y, direction.Z);

    public void setConeAngle(float coneAngle) => NativeBindings.LightRenderer.setConeAngle(_uuid, coneAngle);
}
