#pragma once

// C interface between LuxEngine and LuxForge.dll (the The Forge render backend).
// Only plain data crosses this boundary so the DLL can be built with The Forge's own
// settings and loaded at run time without linking The Forge into the engine.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUX_FORGE_API_VERSION 10u

#if defined(_WIN32)
#if defined(LUX_FORGE_BUILD)
#define LUX_FORGE_EXPORT __declspec(dllexport)
#else
#define LUX_FORGE_EXPORT __declspec(dllimport)
#endif
#else
#define LUX_FORGE_EXPORT
#endif

typedef struct LuxForgeDevice LuxForgeDevice;

#define LUX_FORGE_WINDOW_WIN32 1u
#define LUX_FORGE_WINDOW_X11 2u
#define LUX_FORGE_WINDOW_WAYLAND 3u
#define LUX_FORGE_WINDOW_METAL 4u

typedef struct LuxForgeInitDesc {
    void* nativeWindow;      // HWND, wl_surface, or SDL Metal NSView
    void* nativeDisplay;     // Display* on X11, wl_display* on Wayland
    uint64_t nativeWindowId; // X11 Window
    uint32_t windowType;     // LUX_FORGE_WINDOW_*
    uint32_t width;
    uint32_t height;
    uint32_t framesInFlight;
    int vsync;
    int validation;
    uint64_t maxFrameVertexBytes; // 0 = 256 MiB
} LuxForgeInitDesc;

typedef struct LuxForgeInfo {
    char deviceName[256];
    char apiName[32];
    uint32_t framesInFlight;
    int validation;
    uint32_t apiVersion; // native Vulkan version / D3D feature level; zero if unavailable
    uint32_t driverVersion;
    uint32_t maxTextureSize; // native 2D limit, capped by the engine's supported 16384 limit
    int gpuTimestamps;
} LuxForgeInfo;

// Results of beginFrame.
#define LUX_FORGE_FRAME_READY 0
#define LUX_FORGE_FRAME_SKIPPED 1
#define LUX_FORGE_FRAME_ERROR (-1)

// Vertex layout of drawTriangles (matches LuxEngine::RenderVertex): 48 bytes.
typedef struct LuxForgeVertex {
    float position[3];
    float color[4];
    float uv[2];
    float normal[3];
} LuxForgeVertex;

typedef struct LuxForgeStats {
    uint32_t drawCalls;       // current frame
    uint32_t pipelineBinds;   // current frame
    uint32_t descriptorBinds; // actual cmdBindDescriptorSet calls in the current frame
    uint32_t uniformBinds;    // descriptor binds whose set contains a uniform buffer
    uint64_t vertexBytes;     // current frame
    uint32_t liveTextures;    // excluding the built-in white texture
    uint32_t pendingDeletes;  // resources waiting for the GPU before release
    uint32_t scenePasses;     // renderScene calls in the current frame
    uint64_t textureBytes;
    uint64_t sceneBytes;           // HDR/depth/shadow targets and environment maps of renderScene
    uint64_t swapchainRecreations; // successful recreations, excluding initial creation
} LuxForgeStats;

typedef struct LuxForgePointLight {
    float position[3];
    float radius;
    float color[3];
    float intensity;
} LuxForgePointLight;

typedef struct LuxForgeLighting {
    float direction[3];
    float color[3];
    float intensity;
    float camera[3];
    int pbr;
    uint32_t pointCount; // at most 16 are used
    const LuxForgePointLight* points;
} LuxForgeLighting;

#define LUX_FORGE_BUFFER_VERTEX 0
#define LUX_FORGE_BUFFER_INDEX 1
#define LUX_FORGE_BUFFER_UNIFORM 2
#define LUX_FORGE_BUFFER_STORAGE 3

// One image of an environment (see createEnvironment). RGBA32F texels, level by level; each
// level holds `layers` faces of size x size texels (size halves per level, at least 1).
typedef struct LuxForgeEnvironmentImage {
    uint32_t size;
    uint32_t levels;
    uint32_t layers; // 6 = cube map (+X, -X, +Y, -Y, +Z, -Z), 1 = 2D image
    const float* pixels;
    uint64_t floatCount; // must equal the size of the whole mip chain
} LuxForgeEnvironmentImage;

// Mesh of a renderScene pass. Buffers and textures may already be destroyed during the current
// frame (they stay alive until the GPU is done with it); texture 0 = white.
typedef struct LuxForgeSceneDraw {
    uint32_t vertexBuffer;
    uint32_t indexBuffer;
    uint32_t texture;
    uint32_t indexCount;
    float model[16];
    float albedo[4];
    float metallic;
    float roughness;
    uint32_t alphaMode;
    float alphaCutoff;
    int doubleSided;
    int legacy;
    uint32_t normalTexture, metallicTexture, roughnessTexture, occlusionTexture;
    float normalScale, occlusionStrength;
    uint32_t sampling;
} LuxForgeSceneDraw;

// One HDR scene pass (port of the Vulkan backend's Scene3DRenderer). The vec4 fields and the
// shadow matrix are the scene constants of Shaders/Scene3D.hlsli (SceneData), already computed
// by the engine; matrices are column-major in Vulkan clip space.
typedef struct LuxForgeScene {
    float viewProjection[16];
    float camera[4];      // xyz: position, w: exposure
    float environment[4]; // rgb: ambient colour, w: intensity
    float options[4];     // x: rotation (radians), y: image-based lighting, z: ACES, w: sky
    float right[4];
    float up[4];
    float forward[4];
    float shadowMatrix[16];
    float shadowOptions[4];    // x: on, y: 1/resolution, z: depth bias, w: normal bias
    uint32_t shadowResolution; // 0 = no shadow pass (then shadowOptions[0] must be 0)
    uint32_t environmentMap;   // from createEnvironment
} LuxForgeScene;

typedef struct LuxForgeApi {
    uint32_t version;
    // Returns NULL on failure and writes the reason into error. The Forge keeps renderer-wide
    // state in globals, so only one device can exist at a time; a second create fails.
    LuxForgeDevice* (*create)(const LuxForgeInitDesc* desc, char* error, size_t errorSize);
    void (*destroy)(LuxForgeDevice* device);
    void (*getInfo)(const LuxForgeDevice* device, LuxForgeInfo* info);
    const char* (*lastError)(const LuxForgeDevice* device);

    // Frame loop. All functions returning int return 1 on success and 0 on failure
    // (the reason is in lastError), except beginFrame which returns LUX_FORGE_FRAME_*.
    int (*beginFrame)(LuxForgeDevice* device);
    int (*clear)(LuxForgeDevice* device, float r, float g, float b, float a);
    int (*endFrame)(LuxForgeDevice* device);
    int (*resize)(LuxForgeDevice* device, uint32_t width, uint32_t height);
    int (*setVSync)(LuxForgeDevice* device, int enabled);
    int (*waitIdle)(LuxForgeDevice* device);

    // Capture of the next presented frame. readCapture waits for that frame, then writes
    // width*height*4 RGBA bytes when rgba is large enough; *size always receives the size needed.
    int (*requestCapture)(LuxForgeDevice* device);
    int (*readCapture)(LuxForgeDevice* device, uint32_t* width, uint32_t* height, uint8_t* rgba,
                       size_t capacity, size_t* size);
    uint64_t (*framesPresented)(const LuxForgeDevice* device);
    // 1 once the GPU was removed/reset; the device must then be destroyed.
    int (*deviceLost)(const LuxForgeDevice* device);
    // Current swap chain size.
    void (*size)(const LuxForgeDevice* device, uint32_t* width, uint32_t* height);

    // Textures: RGBA8 pixels (NULL = transparent black). Returns an id > 0, or 0 on failure.
    uint32_t (*createTexture)(LuxForgeDevice* device, uint32_t width, uint32_t height,
                              const void* rgba, int nearest, int srgb, uint32_t mipLevels);
    // Released once the GPU no longer uses it; unknown ids are ignored.
    void (*destroyTexture)(LuxForgeDevice* device, uint32_t texture);
    int (*hasTexture)(const LuxForgeDevice* device, uint32_t texture);
    // Triangle list (vertexCount % 3 == 0) with a column-major view-projection matrix.
    // texture 0 = white. depth = depth test/write without blending (3D), else alpha blended 2D.
    int (*drawTriangles)(LuxForgeDevice* device, const LuxForgeVertex* vertices,
                         uint32_t vertexCount, const float* viewProjection, uint32_t texture,
                         int depth);
    void (*stats)(const LuxForgeDevice* device, LuxForgeStats* stats);

    // GPU buffers (zero-initialised). Updates wait for frames still reading the buffer.
    uint32_t (*createBuffer)(LuxForgeDevice* device, int type, uint32_t sizeBytes);
    int (*updateBuffer)(LuxForgeDevice* device, uint32_t buffer, const void* data,
                        uint32_t sizeBytes, uint32_t offsetBytes);
    void (*destroyBuffer)(LuxForgeDevice* device, uint32_t buffer);
    int (*hasBuffer)(const LuxForgeDevice* device, uint32_t buffer);
    // Lit meshes (port of the Vulkan backend's mesh pipeline: Basic shaders with lit = true).
    int (*setLighting)(LuxForgeDevice* device, const LuxForgeLighting* lighting);
    int (*drawIndexed)(LuxForgeDevice* device, uint32_t vertexBuffer, uint32_t indexBuffer,
                       uint32_t indexCount, const float* viewProjection, const float* model,
                       const float* albedo, uint32_t texture, float metallic, float roughness,
                       int pbr);

    // HDR scene renderer. An environment holds the sky, diffuse and specular cube maps and the
    // 2D BRDF lookup table (images[0..3]); returns an id > 0, or 0 on failure.
    uint32_t (*createEnvironment)(LuxForgeDevice* device, const LuxForgeEnvironmentImage* images);
    // Released once the GPU no longer uses it; unknown ids are ignored.
    void (*destroyEnvironment)(LuxForgeDevice* device, uint32_t environment);
    // Shadow map, HDR pass (sky + lit meshes with the current SetLighting block) and tone mapping
    // onto the swap chain. Pixels no mesh or sky covered keep what was drawn before. Draws that
    // reference dead resources are skipped; *drawn (optional) receives the number rendered.
    int (*renderScene)(LuxForgeDevice* device, const LuxForgeScene* scene,
                       const LuxForgeSceneDraw* draws, uint32_t drawCount, uint32_t* drawn);
} LuxForgeApi;

typedef const LuxForgeApi* (*LuxForgeGetApiFn)(void);
LUX_FORGE_EXPORT const LuxForgeApi* LuxForgeGetApi(void);

#ifdef __cplusplus
}
#endif
