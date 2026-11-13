#ifndef LUX_SCENE_RESOURCES_H
#define LUX_SCENE_RESOURCES_H

#include "LuxStructs.h"
#include "LuxScene.srt.h"

// Port of Shaders/Scene3D.hlsli.
float3 RotateEnvironment(float3 v) {
    const float c = cos(gScene.options.x);
    const float s = sin(gScene.options.x);
    return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

float3 EnvironmentLighting(float3 base, float metal, float rough, float3 n, float3 v) {
    const float nv = saturate(dot(n, v));
    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), base, metal);
    if (gScene.options.y < 0.5f)
        return base * (1.0f - metal) * gScene.environment.rgb * gScene.environment.w;
    const float3 irradiance =
        SampleLvlTexCube(gDiffuseEnv, gSamplerTrilinearClamp, RotateEnvironment(n), 0.0f).rgb;
    const float3 reflected = SampleLvlTexCube(gSpecularEnv, gSamplerTrilinearClamp,
                                              RotateEnvironment(reflect(-v, n)), rough * 7.0f)
                                 .rgb;
    const float2 brdf =
        SampleLvlTex2D(gBrdfLut, gSamplerTrilinearClamp, float2(nv, rough), 0.0f).rg;
    const float3 f = f0 + (max(float3(1.0f - rough, 1.0f - rough, 1.0f - rough), f0) - f0) *
                              pow(1.0f - nv, 5.0f);
    return ((1.0f - f) * (1.0f - metal) * base * irradiance + reflected * (f0 * brdf.x + brdf.y)) *
           gScene.environment.w;
}

float DirectionalShadow(float3 world, float3 normal) {
    if (gScene.shadowOptions.x < 0.5f)
        return 1.0f;
    const float4 clip =
        mul(gScene.shadowMatrix, float4(world + normal * gScene.shadowOptions.w, 1.0f));
    const float3 p = clip.xyz / clip.w;
    const float2 uv = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
    if (uv.x < 0.0f || uv.y < 0.0f || uv.x > 1.0f || uv.y > 1.0f || p.z < 0.0f || p.z > 1.0f)
        return 1.0f;
    const int resolution = int(round(1.0f / gScene.shadowOptions.y));
    const int2 center = int2(uv * float(resolution));
    float visibility = 0.0f;
    UNROLL for (int y = -1; y <= 1; ++y) {
        UNROLL for (int x = -1; x <= 1; ++x) {
            const int2 pixel =
                clamp(center + int2(x, y), int2(0, 0), int2(resolution - 1, resolution - 1));
            visibility +=
                p.z - gScene.shadowOptions.z <= LoadTex2D(gShadowMap, NO_SAMPLER, pixel, 0).r
                    ? 1.0f
                    : 0.0f;
        }
    }
    return visibility / 9.0f;
}

#endif
