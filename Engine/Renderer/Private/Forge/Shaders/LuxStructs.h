#ifndef LUX_STRUCTS_H
#define LUX_STRUCTS_H

// Per-draw constants shared by every LuxForge shader (filled by LuxForge.cpp).
STRUCT(LuxDraw) {
    DATA(float4x4, viewProjection, None);
    DATA(float4x4, model, None);
    DATA(float4, albedo, None);
    // x: sRGB texture, y: metallic, z: roughness, w: PBR (same as the Vulkan backend)
    DATA(float4, colorOptions, None);
    // x: 1 = nearest filtering, 0 = bilinear
    DATA(float4, options, None);
    DATA(float4, materialOptions, None);
};

// Lighting set with SetLighting (16 point lights, like the Vulkan backend).
STRUCT(LuxLights) {
    DATA(float4, direction, None);
    DATA(float4, colorIntensity, None);
    DATA(uint4, count, None);
    DATA(float4, camera, None);
    DATA(float4, pointPositionRadius[16], None);
    DATA(float4, pointColorIntensity[16], None);
};

// Constants of one HDR scene pass (Shaders/Scene3D.hlsli SceneData; filled by LuxForge.cpp).
STRUCT(LuxSceneData) {
    DATA(float4, camera, None);      // xyz: position, w: exposure
    DATA(float4, environment, None); // rgb: ambient colour, w: intensity
    DATA(float4, options, None);     // x: rotation (radians), y: IBL, z: ACES, w: sky
    DATA(float4, right, None);
    DATA(float4, up, None);
    DATA(float4, forward, None);
    DATA(float4x4, shadowMatrix, None);
    DATA(float4, shadowOptions, None); // x: on, y: 1/resolution, z: depth bias, w: normal bias
};

#endif
