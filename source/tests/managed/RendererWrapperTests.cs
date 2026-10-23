using System;
using System.Numerics;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Each component wrapper routes a call to the matching native function with its own uuid, in order, and
// reads vectors, scalars and bools back from what native answered. Values are chosen distinct per axis so a
// swapped or dropped component shows up.
[Collection("BridgeInstances")]
public class RendererWrapperTests : IDisposable
{
  private const string Id = "obj-1";

  private readonly IDisposable _natives = FakeNatives.Install();

  public void Dispose()
  {
    var error = FakeNatives.LastError;
    _natives.Dispose();
    Assert.Null(error);
  }

  // ---- ModelRenderer ----

  [Fact]
  public void ModelRenderer_ReadsAssetUuidsAsUtf8AndNullAsEmpty()
  {
    var renderer = new ModelRenderer(Id);
    FakeNatives.SetString("ModelRenderer.getModelUUID", "model-é");
    FakeNatives.SetString("ModelRenderer.getTextureUUID", null);
    FakeNatives.Set("ModelRenderer.getShouldRender", true);

    Assert.Equal("model-é", renderer.getModelUUID());
    Assert.Equal("", renderer.getTextureUUID());
    Assert.True(renderer.getShouldRender());

    FakeNatives.SetString("ModelRenderer.getTextureUUID", "tex-1");
    FakeNatives.Set("ModelRenderer.getShouldRender", false);
    Assert.Equal("tex-1", renderer.getTextureUUID());
    Assert.False(renderer.getShouldRender());
  }

  [Fact]
  public void ModelRenderer_SetModelAndTexturePassAssetUuidAndReturnNativeResult()
  {
    var renderer = new ModelRenderer(Id);
    FakeNatives.Set("ModelRenderer.setModelUUID", true);
    FakeNatives.Set("ModelRenderer.setTextureUUID", false);

    Assert.True(renderer.setModel("model-é"));
    Assert.False(renderer.setTexture("tex-1"));
    renderer.setShouldRender(true);
    renderer.setShouldRender(false);

    Assert.Equal(new[]
    {
      "ModelRenderer.setModelUUID(obj-1,model-é)",
      "ModelRenderer.setTextureUUID(obj-1,tex-1)",
      "ModelRenderer.setShouldRender(obj-1,1)",
      "ModelRenderer.setShouldRender(obj-1,0)"
    }, FakeNatives.Calls);
  }

  // ---- LightRenderer ----

  [Fact]
  public void LightRenderer_ReadsEveryField()
  {
    var light = new LightRenderer(Id);
    FakeNatives.Set("LightRenderer.getIsSpotLight", true);
    FakeNatives.Set("LightRenderer.getColor", new Vector3(0.25f, 0.5f, 0.75f));
    FakeNatives.Set("LightRenderer.getAmbient", 0.125f);
    FakeNatives.Set("LightRenderer.getDiffuse", 0.5f);
    FakeNatives.Set("LightRenderer.getSpecular", 2f);
    FakeNatives.Set("LightRenderer.getDirection", new Vector3(1f, -2f, 3f));
    FakeNatives.Set("LightRenderer.getConeAngle", 30f);

    Assert.True(light.getIsSpotLight());
    Assert.Equal(new Vector3(0.25f, 0.5f, 0.75f), light.getColor());
    Assert.Equal(0.125f, light.getAmbient());
    Assert.Equal(0.5f, light.getDiffuse());
    Assert.Equal(2f, light.getSpecular());
    Assert.Equal(new Vector3(1f, -2f, 3f), light.getDirection());
    Assert.Equal(30f, light.getConeAngle());

    FakeNatives.Set("LightRenderer.getIsSpotLight", false);
    Assert.False(light.getIsSpotLight());
  }

  [Fact]
  public void LightRenderer_WritesRoute()
  {
    var light = new LightRenderer(Id);

    light.setSpotLight(true);
    light.setSpotLight(false);
    light.setColor(0.25f, 0.5f, 0.75f);
    light.setColor(new Vector3(1f, 0f, 0.5f));
    light.setAmbient(0.125f);
    light.setDiffuse(0.5f);
    light.setSpecular(2f);
    light.setDirection(1f, 2f, 3f);
    light.setDirection(new Vector3(4f, 5f, 6f));
    light.setConeAngle(45f);

    Assert.Equal(new[]
    {
      "LightRenderer.setSpotLight(obj-1,1)",
      "LightRenderer.setSpotLight(obj-1,0)",
      "LightRenderer.setColor(obj-1,0.25,0.5,0.75)",
      "LightRenderer.setColor(obj-1,1,0,0.5)",
      "LightRenderer.setAmbient(obj-1,0.125)",
      "LightRenderer.setDiffuse(obj-1,0.5)",
      "LightRenderer.setSpecular(obj-1,2)",
      "LightRenderer.setDirection(obj-1,1,2,3)",
      "LightRenderer.setDirection(obj-1,4,5,6)",
      "LightRenderer.setConeAngle(obj-1,45)"
    }, FakeNatives.Calls);
  }
}
