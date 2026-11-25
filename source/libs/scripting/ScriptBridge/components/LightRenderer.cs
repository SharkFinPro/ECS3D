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
    public delegate* unmanaged<IntPtr, int> getLightType;
    public delegate* unmanaged<IntPtr, int, void> setLightType;
    public delegate* unmanaged<IntPtr, float> getRange;
    public delegate* unmanaged<IntPtr, float, void> setRange;
    public delegate* unmanaged<IntPtr, float> getShadowDistance;
    public delegate* unmanaged<IntPtr, float, void> setShadowDistance;
}

// Values match the native LightType enum.
public enum LightType
{
    Point = 0,
    Spot = 1,
    Directional = 2
}

// How an object lights the scene: color, the ambient/diffuse/specular strengths, its type (point, spot or
// directional), range (0 is unbounded), shadow distance, and the spot direction/cone angle. Reached via World.tryGetLightRenderer like Transform/
// ModelRenderer, since a script typically changes another object's light rather than its own.
//
// Every setter goes through the native component's own setter, so its rules still apply: a non-finite
// color/strength/direction is ignored, setConeAngle clamps to the component's configured range, setRange and
// setShadowDistance clamp too, and an unknown type is ignored. getIsSpotLight and setSpotLight only see the
// spot/point pair; setSpotLight(false) leaves a directional light alone.
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

    public LightType getLightType() => (LightType)NativeBindings.LightRenderer.getLightType(_uuid);

    public float getRange() => NativeBindings.LightRenderer.getRange(_uuid);

    public float getShadowDistance() => NativeBindings.LightRenderer.getShadowDistance(_uuid);

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

    public void setLightType(LightType type) => NativeBindings.LightRenderer.setLightType(_uuid, (int)type);

    public void setRange(float range) => NativeBindings.LightRenderer.setRange(_uuid, range);

    public void setShadowDistance(float distance) =>
        NativeBindings.LightRenderer.setShadowDistance(_uuid, distance);
}
