#ifndef LUX_SCENE_SRT_H
#define LUX_SCENE_SRT_H

// Descriptor layout of the HDR scene shaders. PerFrame/PerBatch/PerDraw match Lux2D.srt.h so
// the light, texture and draw-constant sets of the mesh path are shared; Persistent holds the
// per-pass scene constants, image-based lighting maps, shadow map and HDR image.
BEGIN_SRT(LuxSceneSrt)
BEGIN_SRT_SET(Persistent)
DECL_CBUFFER(Persistent, CBUFFER(LuxSceneData), gScene)
DECL_TEXTURE(Persistent, TexCube(float4), gDiffuseEnv)
DECL_TEXTURE(Persistent, TexCube(float4), gSpecularEnv)
DECL_TEXTURE(Persistent, TexCube(float4), gSkyEnv)
DECL_TEXTURE(Persistent, Tex2D(float4), gBrdfLut)
DECL_TEXTURE(Persistent, Tex2D(float), gShadowMap)
DECL_TEXTURE(Persistent, Tex2D(float4), gHdrImage)
DECL_SAMPLER(Persistent, SamplerState, gMaterialClamp)
DECL_SAMPLER(Persistent, SamplerState, gMaterialRepeat)
DECL_SAMPLER(Persistent, SamplerState, gMaterialAnisotropicClamp)
DECL_SAMPLER(Persistent, SamplerState, gMaterialAnisotropicRepeat)
END_SRT_SET(Persistent)
BEGIN_SRT_SET(PerFrame)
DECL_CBUFFER(PerFrame, CBUFFER(LuxLights), gLights)
END_SRT_SET(PerFrame)
BEGIN_SRT_SET(PerBatch)
DECL_TEXTURE(PerBatch, Tex2D(float4), gTexture)
DECL_TEXTURE(PerBatch, Tex2D(float4), gNormalTexture)
DECL_TEXTURE(PerBatch, Tex2D(float4), gMetallicTexture)
DECL_TEXTURE(PerBatch, Tex2D(float4), gRoughnessTexture)
DECL_TEXTURE(PerBatch, Tex2D(float4), gOcclusionTexture)
END_SRT_SET(PerBatch)
BEGIN_SRT_SET(PerDraw)
DECL_CBUFFER(PerDraw, CBUFFER(LuxDraw), gDraw)
END_SRT_SET(PerDraw)
END_SRT(LuxSceneSrt)

#endif
