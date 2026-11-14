// The Forge render bridge: D3D12 on Windows, Vulkan on Linux, Metal on macOS.
#include "LuxForgeApi.h"

// Standard headers first: The Forge's IMemory.h turns malloc/free into compile errors.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// clang-format off
#include "Graphics/GraphicsConfig.h"
#include "Graphics/Interfaces/IGraphics.h"
#include "Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_query.h"
#include "OS/Interfaces/IOperatingSystem.h"
#include "Utilities/Interfaces/IFileSystem.h"
#include "Utilities/Interfaces/ILog.h"
#include "Utilities/RingBuffer.h"
#include "Graphics/FSL/fsl_srt.h"
#include "Graphics/FSL/defaults.h"
#include "Shaders/Lux2D.srt.h"
#include "Shaders/LuxScene.srt.h"
#include "Utilities/Interfaces/IMemory.h"
// clang-format on

// IMemory.h turns new/delete into compile errors. LuxForgeDevice is our own plain object
// (never handed to The Forge), so the normal operators are correct here.
#undef new
#undef delete

// The Forge asks the app framework to rebuild the swap chain / renderer on device-loss style
// events (normally WindowsBase.cpp). LuxEngine owns the window, so it just records the request.
static std::atomic<bool> g_ResetRequested = false;

#if defined(_WIN32)
void requestReset(const ResetDesc* desc) {
    g_ResetRequested.store(desc && desc->mType != RESET_TYPE_NONE);
}
#endif

static bool ResetRequested() {
    return g_ResetRequested.load();
}

// Direct3D12.c keeps these internal; the declarations match its definitions.
typedef struct SubresourceDataDesc {
    uint64_t mSrcOffset;
    uint32_t mMipLevel;
    uint32_t mArrayLayer;
#if defined(VULKAN) || defined(METAL)
    uint32_t mRowPitch;
    uint32_t mSlicePitch;
#endif
} SubresourceDataDesc;

extern "C" void cmdCopySubresource(Cmd* pCmd, Buffer* pDstBuffer, Texture* pTexture,
                                   const SubresourceDataDesc* pSubresourceDesc);

struct LuxForgeDevice {
    Renderer* renderer = nullptr;
    Queue* graphicsQueue = nullptr;
    uint32_t framesInFlight = 2;
    bool validation = false;
    std::string lastError;

    WindowHandle window = {};
    SwapChain* swapChain = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    bool vsync = true;
    GpuCmdRing cmdRing = {};
    // Acquisition waits belong to command-ring slots; presentation waits belong to images.
    std::vector<Semaphore*> imageAcquired;
    std::vector<Semaphore*> renderFinished;

    // Current frame
    bool frameOpen = false;
    uint32_t imageIndex = 0;
    GpuCmdRingElement element = {};
    uint64_t framesPresented = 0;

    // Frame capture
    bool captureRequested = false;
    bool captureRecorded = false;
    Buffer* captureBuffer = nullptr;
    Fence* captureFence = nullptr; // owned by cmdRing
    uint32_t captureWidth = 0;
    uint32_t captureHeight = 0;
    uint32_t captureRowPitch = 0;

    // Drawing
    static constexpr uint32_t kMaxTextures = 4096;
    static constexpr uint32_t kMaxDraws = 16384; // per frame
    static constexpr uint32_t kDrawStride = 256; // constant-buffer alignment
    static constexpr uint32_t kVertexStride = sizeof(LuxForgeVertex);

    struct FrameResources {
        DescriptorSet* drawSet = nullptr;
        Buffer* uniforms = nullptr;
        Buffer* vertices = nullptr;
        uint64_t vertexCapacity = 0;
        uint64_t vertexUsed = 0;
        uint32_t draws = 0;
        std::vector<Buffer*>
            retired; // replaced vertex buffers still used by this slot's last frame
    };

    struct TextureSlot {
        Texture* texture = nullptr;
        bool nearest = false;
        bool srgb = false;
        uint64_t bytes = 0;
    };

    struct BufferSlot {
        Buffer* buffer = nullptr;
        uint32_t size = 0;
        int type = 0;
        uint64_t lastUsedFrame = 0; // frame number (1-based) of the last frame drawing with it
        bool pendingDelete = false;
    };

    struct PendingBuffer {
        uint64_t frame;
        uint32_t id;
    };

    static constexpr uint32_t kMaxLightBlocks = 256; // SetLighting calls per frame
    static constexpr uint32_t kLightStride = 768;    // 576-byte block, CBV aligned

    struct PendingTexture {
        uint64_t frame;
        uint32_t id;
    };

    Shader* shaderMesh = nullptr;
    Pipeline* pipelineMesh = nullptr;
    DescriptorSet* lightSet = nullptr;
    Buffer* lightBuffers[8] = {};
    uint32_t lightBlocksUsed = 0; // in the current frame
    uint32_t currentLight = 0;
    std::vector<BufferSlot> buffers = std::vector<BufferSlot>(1); // id 0 unused
    std::vector<uint32_t> freeBufferIds;
    std::vector<PendingBuffer> pendingBuffers;
    Shader* shader2D = nullptr;
    Pipeline* pipeline2D = nullptr;
    Pipeline* pipelineDepth = nullptr;
    DescriptorSet* textureSet = nullptr;
    RenderTarget* depth = nullptr;
    FrameResources frames[8];
    uint32_t frameSlot = 0;
    uint64_t frameNumber = 0;     // frames submitted
    uint64_t completedFrames = 0; // frames known to be finished on the GPU
    uint64_t slotSubmitted[8] = {};
    uint64_t maxFrameVertexBytes = 256ull << 20;
    std::vector<TextureSlot> textures = std::vector<TextureSlot>(kMaxTextures + 1);
    std::vector<uint32_t> freeTextureIds;
    std::vector<PendingTexture> pendingTextures;
    uint32_t whiteTexture = 0;
    uint32_t liveTextures = 0;
    bool passOpen = false;
    Pipeline* boundPipeline = nullptr;
    uint32_t boundTexture = 0;
    LuxForgeStats frameStats = {};

    // HDR scene (renderScene). The targets are shared by every pass and frame: one queue runs
    // the passes in order and barriers separate each write from the reads before it.
    static constexpr uint32_t kMaxScenePasses = 16;  // per frame, like the Vulkan backend
    static constexpr uint32_t kSceneStride = 256;    // LuxSceneData block, CBV aligned
    static constexpr uint32_t kSceneIdleFrames = 60; // unused targets are released after this

    struct SceneTarget {
        RenderTarget* target = nullptr;
        ResourceState state = RESOURCE_STATE_UNDEFINED;
        uint64_t lastUsedFrame = 0;
        uint64_t bytes = 0;
    };

    struct PendingTarget {
        uint64_t frame;
        RenderTarget* target;
    };

    struct EnvironmentSlot {
        Texture* maps[4] = {}; // sky, diffuse, specular, BRDF
        uint64_t bytes = 0;
        bool pendingDelete = false;
    };

    struct PendingEnvironment {
        uint64_t frame;
        uint32_t id;
    };

    bool sceneInitAttempted = false;
    std::string sceneInitError;
    Shader* shaderScene = nullptr;
    Shader* shaderShadow = nullptr;
    Shader* shaderSky = nullptr;
    Shader* shaderTone = nullptr;
    DescriptorSet* materialSets[8][2] = {};
    Sampler* materialSamplers[4] = {};
    static constexpr uint32_t kMaterialBankSize = 8192;
    Pipeline* pipelineScene = nullptr;
    Pipeline* materialPipelines[6] = {};
    Pipeline* pipelineShadow = nullptr;
    Pipeline* pipelineSky = nullptr;
    Pipeline* pipelineTone = nullptr;
    DescriptorSet* sceneSet = nullptr; // two entries per pass: scene, tone
    Buffer* sceneUniforms[8] = {};
    uint32_t scenePasses = 0;         // in the current frame
    uint64_t lastShadowlessFrame = 0; // last frame with a scene pass that had shadows off
    SceneTarget hdr, sceneDepth, shadow;
    uint32_t shadowResolution = 0;
    std::vector<PendingTarget> pendingTargets;
    std::vector<EnvironmentSlot> environments = std::vector<EnvironmentSlot>(1); // id 0 unused
    std::vector<uint32_t> freeEnvironmentIds;
    std::vector<PendingEnvironment> pendingEnvironments;
    uint64_t sceneBytes = 0;
};

namespace {
#if defined(DIRECT3D12)
constexpr DescriptorType UniformBufferDescriptor = DESCRIPTOR_TYPE_UNDEFINED;
#else
constexpr DescriptorType UniformBufferDescriptor = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
#endif

void CollectSceneGarbage(LuxForgeDevice* device, bool everything);
void ShutdownScene(LuxForgeDevice* device);
void BeginSceneFrame(LuxForgeDevice* device);
int EndFrame(LuxForgeDevice* device);

// The Forge's memory, file and log systems, its GPU configuration and resource loader are
// process-wide. Viewports share that renderer and own independent queues and swap chains.
int g_PlatformUsers = 0;
std::mutex g_DeviceMutex;
Renderer* g_SharedRenderer = nullptr;
uint32_t g_RendererUsers = 0;
bool g_SharedRootSignatures = false;
std::mutex g_InitializationLogMutex;
std::string g_InitializationError;
bool g_CaptureInitializationError = false;

void CaptureInitializationError(void*, const char* message) {
    std::lock_guard<std::mutex> lock(g_InitializationLogMutex);
    if (!g_CaptureInitializationError || !g_InitializationError.empty())
        return;
    const char* text = std::strstr(message, "ERR| ");
    g_InitializationError = text ? text + 5 : message;
    while (!g_InitializationError.empty() &&
           (g_InitializationError.back() == '\n' || g_InitializationError.back() == '\r'))
        g_InitializationError.pop_back();
}

bool AcquirePlatform(std::string& error) {
    if (g_PlatformUsers++ > 0)
        return true;
    if (!initMemAlloc("LuxForge")) {
        error = "The Forge memory system failed to start";
        --g_PlatformUsers;
        return false;
    }
    FileSystemInitDesc fs = {};
    fs.pAppName = "LuxForge";
    fs.mIsTool = true; // no PathStatement file; resource folders are set explicitly
    if (!initFileSystem(&fs)) {
        exitMemAlloc();
        error = "The Forge file system failed to start";
        --g_PlatformUsers;
        return false;
    }
    // Without the app framework's PathStatement file no resource directory has a file system
    // attached; point every built-in directory at the folder holding LuxForge.dll (gpu.data lives
    // there) so The Forge never dereferences an unset file system.
    static std::string dllDir;
    if (dllDir.empty()) {
#if defined(_WIN32)
        HMODULE self = nullptr;
        wchar_t path[32768] = {};
        const bool foundModule =
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&AcquirePlatform), &self) != 0;
        const DWORD length = foundModule ? GetModuleFileNameW(self, path, 32768) : 0;
        const int bytes =
            length && length < 32768
                ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, static_cast<int>(length),
                                      nullptr, 0, nullptr, nullptr)
                : 0;
        std::string utf8Directory(static_cast<size_t>(bytes), '\0');
        if (!bytes ||
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, static_cast<int>(length),
                                utf8Directory.data(), bytes, nullptr, nullptr) != bytes) {
            error = "The Forge renderer directory could not be resolved as UTF-8";
            exitFileSystem();
            exitMemAlloc();
            --g_PlatformUsers;
            return false;
        }
        dllDir = std::move(utf8Directory);
#else
        Dl_info module = {};
        if (!dladdr(reinterpret_cast<void*>(&AcquirePlatform), &module) || !module.dli_fname) {
            error = "The Forge renderer directory could not be resolved";
            exitFileSystem();
            exitMemAlloc();
            --g_PlatformUsers;
            return false;
        }
        dllDir = module.dli_fname;
#endif
        dllDir.resize(dllDir.find_last_of("\\/") + 1);
    }
    // The directory table is a process-wide global that survives exitFileSystem(); setting it a
    // second time would log a warning before the log system exists again, so do it only once.
    static bool resourceDirsSet = false;
    if (!resourceDirsSet) {
        for (int rd = 0; rd <= RD_OTHER_FILES; ++rd)
            fsSetPathForResourceDir(pSystemFileIO, static_cast<ResourceDirectory>(rd),
                                    dllDir.c_str());
        resourceDirsSet = true;
    }
    initLog("LuxForge", static_cast<LogLevel>(LogLevel::eWARNING | LogLevel::eERROR));
    addLogCallback("LuxForgeInitialization", LogLevel::eERROR, nullptr, CaptureInitializationError,
                   nullptr, nullptr);
    return true;
}

void ReleasePlatform() {
    if (g_PlatformUsers <= 0 || --g_PlatformUsers > 0)
        return;
    exitLog();
    exitFileSystem();
    exitMemAlloc();
}

void CopyText(char* out, size_t size, const std::string& text) {
    if (!out || size == 0)
        return;
    std::snprintf(out, size, "%s", text.c_str());
}

bool Fail(LuxForgeDevice* device, const char* message) {
    device->lastError = message;
    return false;
}

void RemoveSwapChain(LuxForgeDevice* device) {
    for (auto* semaphore : device->renderFinished)
        exitSemaphore(device->renderer, semaphore);
    device->renderFinished.clear();
    if (device->swapChain)
        removeSwapChain(device->renderer, device->swapChain);
    device->swapChain = nullptr;
}

bool AddSwapChain(LuxForgeDevice* device) {
    SwapChainDesc desc = {};
    desc.mWindowHandle = device->window;
    desc.ppPresentQueues = &device->graphicsQueue;
    desc.mPresentQueueCount = 1;
    desc.mImageCount = getRecommendedSwapchainImageCount(device->renderer, &desc.mWindowHandle);
    if (desc.mImageCount < device->framesInFlight + 1)
        desc.mImageCount = device->framesInFlight + 1;
    desc.mWidth = device->width;
    desc.mHeight = device->height;
    // Same as the Vulkan backend: linear 8-bit BGRA, shaders write display-ready values.
    desc.mColorFormat = TinyImageFormat_B8G8R8A8_UNORM;
    desc.mColorSpace = COLOR_SPACE_SDR_SRGB;
    desc.mEnableVsync = device->vsync;
    desc.mUseFlipSwapEffect = true;
    addSwapChain(device->renderer, &desc, &device->swapChain);
    if (!device->swapChain)
        return false;
    device->renderFinished.resize(device->swapChain->mImageCount);
    for (auto& semaphore : device->renderFinished)
        initSemaphore(device->renderer, &semaphore);
    return true;
}

void RemoveCaptureBuffer(LuxForgeDevice* device) {
    if (device->captureBuffer) {
        removeResource(device->captureBuffer);
        device->captureBuffer = nullptr;
    }
    device->captureRecorded = false;
}

// Diagnostic: report the first step after which the D3D12 device is gone (LUX_FORGE_TRACE=1).
bool DeviceAlive(LuxForgeDevice* device, const char* step) {
#if defined(DIRECT3D12)
    const char* trace = (std::getenv)("LUX_FORGE_TRACE");
    const HRESULT reason = device->renderer->mDx.pDevice->GetDeviceRemovedReason();
    if ((trace && trace[0] == '1') || FAILED(reason)) {
        std::fprintf(stderr, "[LuxForge] %s: device status 0x%08lX\n", step,
                     static_cast<unsigned long>(reason));
    }
    return SUCCEEDED(reason);
#else
    (void)device;
    (void)step;
    return !ResetRequested();
#endif
}

bool AddDepthTarget(LuxForgeDevice* device) {
    RenderTargetDesc desc = {};
    desc.mWidth = device->width;
    desc.mHeight = device->height;
    desc.mDepth = 1;
    desc.mFlags = TEXTURE_CREATION_FLAG_FORCE_2D;
    desc.mArraySize = 1;
    desc.mMipLevels = 1;
    desc.mSampleCount = SAMPLE_COUNT_1;
    desc.mFormat = TinyImageFormat_D32_SFLOAT;
    desc.mStartState = RESOURCE_STATE_DEPTH_WRITE;
    desc.mClearValue.depth = 1.0f;
    desc.mClearValue.stencil = 0;
    desc.pName = "LuxForge depth";
    addRenderTarget(device->renderer, &desc, &device->depth);
    return device->depth != nullptr;
}

void RemoveDepthTarget(LuxForgeDevice* device) {
    if (device->depth) {
        removeRenderTarget(device->renderer, device->depth);
        device->depth = nullptr;
    }
}

void ReleaseTexture(LuxForgeDevice* device, uint32_t id) {
    auto& slot = device->textures[id];
    if (slot.texture)
        removeResource(slot.texture);
    slot = {};
    device->freeTextureIds.push_back(id);
}

// Frees textures whose last possible use is a frame the GPU has finished.
void CollectGarbage(LuxForgeDevice* device, bool everything) {
    auto& pending = device->pendingTextures;
    size_t kept = 0;
    for (size_t i = 0; i < pending.size(); ++i) {
        if (everything || pending[i].frame <= device->completedFrames)
            ReleaseTexture(device, pending[i].id);
        else
            pending[kept++] = pending[i];
    }
    pending.resize(kept);
    auto& buffers = device->pendingBuffers;
    kept = 0;
    for (size_t i = 0; i < buffers.size(); ++i) {
        if (everything || buffers[i].frame <= device->completedFrames) {
            auto& slot = device->buffers[buffers[i].id];
            if (slot.buffer)
                removeResource(slot.buffer);
            slot = {};
            device->freeBufferIds.push_back(buffers[i].id);
        } else {
            buffers[kept++] = buffers[i];
        }
    }
    buffers.resize(kept);
    CollectSceneGarbage(device, everything);
}

uint32_t CreateTextureImpl(LuxForgeDevice* device, uint32_t width, uint32_t height,
                           const void* rgba, bool nearest, bool srgb, uint32_t mipLevels = 1) {
    if (width == 0 || height == 0 || width > 16384 || height > 16384) {
        device->lastError = "Texture size must be 1..16384";
        return 0;
    }
    if (device->freeTextureIds.empty()) {
        device->lastError = "Too many live textures for the Forge backend";
        return 0;
    }
    // Textures with a dimension of 1 sample as zero after the resource loader upload (seen
    // on D3D12 / RTX 5070), so store them with that dimension doubled. Duplicated texels sample
    // exactly like the original with clamp-to-edge, for both nearest and linear filtering.
    std::vector<uint8_t> padded;
    if (mipLevels == 1 && (width == 1 || height == 1)) {
        const uint32_t paddedWidth = width == 1 ? 2 : width;
        const uint32_t paddedHeight = height == 1 ? 2 : height;
        padded.assign(static_cast<size_t>(paddedWidth) * paddedHeight * 4, 0);
        if (rgba)
            for (uint32_t y = 0; y < paddedHeight; ++y)
                for (uint32_t x = 0; x < paddedWidth; ++x)
                    std::memcpy(&padded[(static_cast<size_t>(y) * paddedWidth + x) * 4],
                                static_cast<const uint8_t*>(rgba) +
                                    (static_cast<size_t>(height == 1 ? 0 : y) * width +
                                     (width == 1 ? 0 : x)) *
                                        4,
                                4);
        width = paddedWidth;
        height = paddedHeight;
        rgba = padded.data();
    }
    TextureDesc desc = {};
    desc.mWidth = width;
    desc.mHeight = height;
    desc.mDepth = 1;
    desc.mArraySize = 1;
    desc.mMipLevels = mipLevels;
    desc.mFlags = TEXTURE_CREATION_FLAG_FORCE_2D;
    desc.mSampleCount = SAMPLE_COUNT_1;
    desc.mFormat = srgb ? TinyImageFormat_R8G8B8A8_SRGB : TinyImageFormat_R8G8B8A8_UNORM;
    desc.mStartState = RESOURCE_STATE_SHADER_RESOURCE;
    desc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
    desc.pName = "LuxForge texture";
    Texture* texture = nullptr;
    TextureLoadDesc load = {};
    load.pDesc = &desc;
    load.ppTexture = &texture;
    addResource(&load, nullptr);
    waitForAllResourceLoads();
    if (!texture) {
        device->lastError = "Texture creation failed";
        return 0;
    }
    size_t sourceOffset = 0;
    for (uint32_t level = 0; level < mipLevels; ++level) {
        const uint32_t mipWidth = std::max(1u, width >> level);
        const uint32_t mipHeight = std::max(1u, height >> level);
        TextureUpdateDesc update = {texture, level, 1, 0, 1, RESOURCE_STATE_SHADER_RESOURCE};
        beginUpdateResource(&update);
        TextureSubresourceUpdate sub = update.getSubresourceUpdateDesc(level, 0);
        const uint32_t rowBytes = mipWidth * 4;
        for (uint32_t row = 0; row < sub.mRowCount; ++row) {
            auto* dst = sub.pMappedData + static_cast<size_t>(row) * sub.mDstRowStride;
            if (rgba)
                std::memcpy(dst,
                            static_cast<const uint8_t*>(rgba) + sourceOffset +
                                static_cast<size_t>(row) * rowBytes,
                            rowBytes);
            else
                std::memset(dst, 0, rowBytes);
        }
        endUpdateResource(&update);
        sourceOffset += static_cast<size_t>(mipWidth) * mipHeight * 4;
    }
    // Texture updates are recorded by the resource loader's upload engine and only submitted
    // on flushResourceUpdates; wait for that copy so the texture is ready before first use.
    FlushResourceUpdateDesc flush = {};
    flushResourceUpdates(&flush);
    if (flush.pOutFence)
        waitForFences(device->renderer, 1, &flush.pOutFence);

    const uint32_t id = device->freeTextureIds.back();
    device->freeTextureIds.pop_back();
    device->textures[id].texture = texture;
    device->textures[id].bytes = sourceOffset;
    device->textures[id].nearest = nearest;
    device->textures[id].srgb = srgb;
    DescriptorData param = {};
    param.mIndex = SRT_RES_IDX(Lux2DSrt, PerBatch, gTexture);
    param.ppTextures = &device->textures[id].texture;
    updateDescriptorSet(device->renderer, id, device->textureSet, 1, &param);
    return id;
}

bool Init2D(LuxForgeDevice* device, std::string& error) {
    ShaderLoadDesc shader = {};
    shader.mVert.pFileName = "lux2d.vert";
    shader.mFrag.pFileName = "lux2d.frag";
    if (const char* diagnostic = (std::getenv)("LUX_FORGE_DIAG")) {
        if (std::strcmp(diagnostic, "color") == 0)
            shader.mFrag.pFileName = "lux2d_diag_color.frag";
        else if (std::strcmp(diagnostic, "texture") == 0)
            shader.mFrag.pFileName = "lux2d_diag_texture.frag";
    }
    DeviceAlive(device, "before shaders");
    addShader(device->renderer, &shader, &device->shader2D);
    DeviceAlive(device, "after shaders");
#if defined(DIRECT3D12)
    if (!device->renderer->mDx.pGraphicsRootSignature) {
        error =
            "LuxForge root signature (DIRECT3D12/default.rootsig) is missing next to LuxForge.dll";
        return false;
    }
#endif
    if (!device->shader2D) {
        error =
            "LuxForge shaders (DIRECT3D12/lux2d.vert, lux2d.frag) are missing next to LuxForge.dll";
        return false;
    }
    DescriptorSetDesc textureSet =
        SRT_SET_DESC(Lux2DSrt, PerBatch, LuxForgeDevice::kMaxTextures + 1, 0);
    addDescriptorSet(device->renderer, &textureSet, &device->textureSet);
    DeviceAlive(device, "after descriptor sets");
    if (!device->textureSet) {
        error = "Texture descriptor set could not be created";
        return false;
    }
    static_assert(LuxForgeDevice::kMaxDraws <= UINT16_MAX);
    DescriptorSetDesc drawSet = SRT_SET_DESC(Lux2DSrt, PerDraw, LuxForgeDevice::kMaxDraws, 0);
    for (uint32_t frame = 0; frame < device->framesInFlight; ++frame) {
        addDescriptorSet(device->renderer, &drawSet, &device->frames[frame].drawSet);
        if (!device->frames[frame].drawSet) {
            error = "Draw descriptor set could not be created";
            return false;
        }
        BufferLoadDesc load = {};
        load.ppBuffer = &device->frames[frame].uniforms;
        load.mDesc.mSize =
            static_cast<uint64_t>(LuxForgeDevice::kMaxDraws) * LuxForgeDevice::kDrawStride;
        load.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        load.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        // No whole-buffer view: a CBV over 64 KiB is invalid (D3D12 removes the device).
        // Each draw gets its own 256-byte CBV through DescriptorDataRange instead.
        load.mDesc.mDescriptors = UniformBufferDescriptor;
        load.mDesc.pName = "LuxForge draw constants";
        DeviceAlive(device, "before uniform buffer");
        addResource(&load, nullptr);
        waitForAllResourceLoads();
        DeviceAlive(device, "after uniform buffer");
        Buffer* uniforms = device->frames[frame].uniforms;
        if (!uniforms || !uniforms->pCpuMappedAddress) {
            error = "Draw constant buffer could not be created";
            return false;
        }
        for (uint32_t draw = 0; draw < LuxForgeDevice::kMaxDraws; ++draw) {
            DescriptorDataRange range = {draw * LuxForgeDevice::kDrawStride,
                                         LuxForgeDevice::kDrawStride, 0};
            DescriptorData param = {};
            param.mIndex = SRT_RES_IDX(Lux2DSrt, PerDraw, gDraw);
            param.ppBuffers = &uniforms;
            param.pRanges = &range;
            updateDescriptorSet(device->renderer, draw, device->frames[frame].drawSet, 1, &param);
        }
    }

    VertexLayout layout = {};
    layout.mBindingCount = 1;
    layout.mBindings[0].mStride = LuxForgeDevice::kVertexStride;
    layout.mAttribCount = 4;
    const ShaderSemantic semantics[4] = {SEMANTIC_POSITION, SEMANTIC_COLOR, SEMANTIC_TEXCOORD0,
                                         SEMANTIC_NORMAL};
    const TinyImageFormat formats[4] = {
        TinyImageFormat_R32G32B32_SFLOAT, TinyImageFormat_R32G32B32A32_SFLOAT,
        TinyImageFormat_R32G32_SFLOAT, TinyImageFormat_R32G32B32_SFLOAT};
    const uint32_t offsets[4] = {offsetof(LuxForgeVertex, position),
                                 offsetof(LuxForgeVertex, color), offsetof(LuxForgeVertex, uv),
                                 offsetof(LuxForgeVertex, normal)};
    for (uint32_t i = 0; i < 4; ++i) {
        layout.mAttribs[i].mSemantic = semantics[i];
        layout.mAttribs[i].mFormat = formats[i];
        layout.mAttribs[i].mBinding = 0;
        layout.mAttribs[i].mLocation = i;
        layout.mAttribs[i].mOffset = offsets[i];
    }

    // Same states as the Vulkan backend: 2D = straight-alpha blending, no depth;
    // depth = opaque with depth test/write (LEQUAL).
    BlendStateDesc blend = {};
    blend.mSrcFactors[0] = BC_SRC_ALPHA;
    blend.mDstFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
    blend.mBlendModes[0] = BM_ADD;
    blend.mSrcAlphaFactors[0] = BC_ONE;
    blend.mDstAlphaFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
    blend.mBlendAlphaModes[0] = BM_ADD;
    blend.mColorWriteMasks[0] = COLOR_MASK_ALL;
    blend.mRenderTargetMask = BLEND_STATE_TARGET_0;
    DepthStateDesc noDepth = {};
    DepthStateDesc depthTest = {};
    depthTest.mDepthTest = true;
    depthTest.mDepthWrite = true;
    depthTest.mDepthFunc = CMP_LEQUAL;
    RasterizerStateDesc raster = {};
    raster.mCullMode = CULL_MODE_NONE;

    TinyImageFormat colorFormat =
        static_cast<TinyImageFormat>(device->swapChain->ppRenderTargets[0]->mFormat);
    PipelineDesc desc = {};
    desc.mType = PIPELINE_TYPE_GRAPHICS;
    PIPELINE_LAYOUT_DESC(desc, NULL, SRT_LAYOUT_DESC(Lux2DSrt, PerFrame),
                         SRT_LAYOUT_DESC(Lux2DSrt, PerBatch), SRT_LAYOUT_DESC(Lux2DSrt, PerDraw));
    GraphicsPipelineDesc& graphics = desc.mGraphicsDesc;
    graphics.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
    graphics.mRenderTargetCount = 1;
    graphics.pColorFormats = &colorFormat;
    graphics.mSampleCount = SAMPLE_COUNT_1;
    graphics.mDepthStencilFormat = TinyImageFormat_D32_SFLOAT;
    graphics.pShaderProgram = device->shader2D;
    graphics.pVertexLayout = &layout;
    graphics.pRasterizerState = &raster;
    graphics.pBlendState = &blend;
    graphics.pDepthState = &noDepth;
    DeviceAlive(device, "before pipelines");
    addPipeline(device->renderer, &desc, &device->pipeline2D);
    graphics.pBlendState = nullptr;
    graphics.pDepthState = &depthTest;
    addPipeline(device->renderer, &desc, &device->pipelineDepth);

    // Lit meshes: same states as the depth pipeline (opaque, depth test/write), lit shader.
    ShaderLoadDesc meshShader = {};
    meshShader.mVert.pFileName = "luxmesh.vert";
    meshShader.mFrag.pFileName = "luxmesh.frag";
    addShader(device->renderer, &meshShader, &device->shaderMesh);
    if (!device->shaderMesh) {
        error = "LuxForge mesh shaders (DIRECT3D12/luxmesh.vert, luxmesh.frag) are missing";
        return false;
    }
    graphics.pShaderProgram = device->shaderMesh;
    addPipeline(device->renderer, &desc, &device->pipelineMesh);
    DeviceAlive(device, "after pipelines");
    if (!device->pipeline2D || !device->pipelineDepth || !device->pipelineMesh) {
        error = "Pipelines could not be created";
        return false;
    }

    DescriptorSetDesc lightSet = SRT_SET_DESC(
        Lux2DSrt, PerFrame, device->framesInFlight * LuxForgeDevice::kMaxLightBlocks, 0);
    addDescriptorSet(device->renderer, &lightSet, &device->lightSet);
    if (!device->lightSet) {
        error = "Light descriptor set could not be created";
        return false;
    }
    for (uint32_t frame = 0; frame < device->framesInFlight; ++frame) {
        BufferLoadDesc load = {};
        load.ppBuffer = &device->lightBuffers[frame];
        load.mDesc.mSize =
            static_cast<uint64_t>(LuxForgeDevice::kMaxLightBlocks) * LuxForgeDevice::kLightStride;
        load.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        load.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        load.mDesc.mDescriptors =
            UniformBufferDescriptor; // per-block CBVs below (see draw constants)
        load.mDesc.pName = "LuxForge lights";
        addResource(&load, nullptr);
        waitForAllResourceLoads();
        Buffer* lights = device->lightBuffers[frame];
        if (!lights || !lights->pCpuMappedAddress) {
            error = "Light buffer could not be created";
            return false;
        }
        std::memset(lights->pCpuMappedAddress, 0, static_cast<size_t>(load.mDesc.mSize));
        for (uint32_t block = 0; block < LuxForgeDevice::kMaxLightBlocks; ++block) {
            DescriptorDataRange range = {block * LuxForgeDevice::kLightStride,
                                         LuxForgeDevice::kLightStride, 0};
            DescriptorData param = {};
            param.mIndex = SRT_RES_IDX(Lux2DSrt, PerFrame, gLights);
            param.ppBuffers = &lights;
            param.pRanges = &range;
            updateDescriptorSet(device->renderer, frame * LuxForgeDevice::kMaxLightBlocks + block,
                                device->lightSet, 1, &param);
        }
    }

    for (uint32_t id = LuxForgeDevice::kMaxTextures; id >= 1; --id)
        device->freeTextureIds.push_back(id);
    const uint8_t white[4] = {255, 255, 255, 255};
    device->whiteTexture = CreateTextureImpl(device, 1, 1, white, false, false);
    if (!device->whiteTexture) {
        error = "White texture could not be created";
        return false;
    }
    return true;
}

void Shutdown2D(LuxForgeDevice* device) {
    CollectGarbage(device, true);
    for (uint32_t id = 1; id <= LuxForgeDevice::kMaxTextures; ++id)
        if (device->textures[id].texture) {
            removeResource(device->textures[id].texture);
            device->textures[id] = {};
        }
    for (auto& frame : device->frames) {
        for (Buffer* buffer : frame.retired)
            removeResource(buffer);
        frame.retired.clear();
        if (frame.vertices)
            removeResource(frame.vertices);
        if (frame.drawSet)
            removeDescriptorSet(device->renderer, frame.drawSet);
        if (frame.uniforms)
            removeResource(frame.uniforms);
        frame = {};
    }
    for (size_t id = 1; id < device->buffers.size(); ++id)
        if (device->buffers[id].buffer)
            removeResource(device->buffers[id].buffer);
    device->buffers.assign(1, {});
    device->freeBufferIds.clear();
    device->pendingBuffers.clear();
    for (Buffer*& lights : device->lightBuffers)
        if (lights) {
            removeResource(lights);
            lights = nullptr;
        }
    if (device->lightSet)
        removeDescriptorSet(device->renderer, device->lightSet);
    device->lightSet = nullptr;
    if (device->pipelineMesh)
        removePipeline(device->renderer, device->pipelineMesh);
    device->pipelineMesh = nullptr;
    if (device->shaderMesh)
        removeShader(device->renderer, device->shaderMesh);
    device->shaderMesh = nullptr;
    if (device->pipeline2D)
        removePipeline(device->renderer, device->pipeline2D);
    if (device->pipelineDepth)
        removePipeline(device->renderer, device->pipelineDepth);
    if (device->textureSet)
        removeDescriptorSet(device->renderer, device->textureSet);
    if (device->shader2D)
        removeShader(device->renderer, device->shader2D);
    device->pipeline2D = device->pipelineDepth = nullptr;
    device->textureSet = nullptr;
    device->shader2D = nullptr;
    RemoveDepthTarget(device);
}

// Binds the swap-chain image and depth buffer, optionally clearing both.
void BindPass(LuxForgeDevice* device, bool clear, const float color[4]) {
    Cmd* cmd = device->element.pCmds[0];
    if (device->passOpen)
        cmdBindRenderTargets(cmd, nullptr);
    BindRenderTargetsDesc bind = {};
    bind.mRenderTargetCount = 1;
    bind.mRenderTargets[0].pRenderTarget = device->swapChain->ppRenderTargets[device->imageIndex];
    bind.mRenderTargets[0].mLoadAction = clear ? LOAD_ACTION_CLEAR : LOAD_ACTION_LOAD;
    bind.mRenderTargets[0].mStoreAction = STORE_ACTION_STORE;
    if (clear) {
        bind.mRenderTargets[0].mClearValue = {{color[0], color[1], color[2], color[3]}};
        bind.mRenderTargets[0].mOverrideClearValue = 1;
    }
    bind.mDepthStencil.pDepthStencil = device->depth;
    bind.mDepthStencil.mLoadAction = clear ? LOAD_ACTION_CLEAR : LOAD_ACTION_LOAD;
    bind.mDepthStencil.mStoreAction = STORE_ACTION_STORE;
    bind.mDepthStencil.mLoadActionStencil = LOAD_ACTION_DONTCARE;
    bind.mDepthStencil.mStoreActionStencil = STORE_ACTION_DONTCARE;
    cmdBindRenderTargets(cmd, &bind);
    cmdSetViewport(cmd, 0.0f, 0.0f, static_cast<float>(device->width),
                   static_cast<float>(device->height), 0.0f, 1.0f);
    cmdSetScissor(cmd, 0, 0, device->width, device->height);
    device->passOpen = true;
    device->boundPipeline = nullptr;
    device->boundTexture = 0;
}

void EndPass(LuxForgeDevice* device) {
    if (device->passOpen) {
        cmdBindRenderTargets(device->element.pCmds[0], nullptr);
        device->passOpen = false;
    }
}

bool EnsureVertexCapacity(LuxForgeDevice* device, LuxForgeDevice::FrameResources& frame,
                          uint64_t bytes) {
    if (frame.vertices && frame.vertexUsed + bytes <= frame.vertexCapacity)
        return true;
    uint64_t capacity = frame.vertexCapacity ? frame.vertexCapacity : (1ull << 20);
    while (capacity < bytes)
        capacity *= 2;
    if (frame.vertices && capacity == frame.vertexCapacity)
        capacity *= 2;
    if (capacity > device->maxFrameVertexBytes)
        capacity = device->maxFrameVertexBytes;
    if (capacity < bytes) {
        device->lastError = "Frame vertex data exceeds maxFrameVertexBytes";
        return false;
    }
    Buffer* buffer = nullptr;
    BufferLoadDesc load = {};
    load.ppBuffer = &buffer;
    load.mDesc.mSize = capacity;
    load.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    load.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    load.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
    load.mDesc.pName = "LuxForge frame vertices";
    addResource(&load, nullptr);
    waitForAllResourceLoads();
    if (!buffer || !buffer->pCpuMappedAddress) {
        device->lastError = "Vertex buffer allocation failed";
        return false;
    }
    if (frame.vertices)
        frame.retired.push_back(frame.vertices); // earlier draws of this frame still read it
    frame.vertices = buffer;
    frame.vertexCapacity = capacity;
    frame.vertexUsed = 0;
    return true;
}

void Destroy(LuxForgeDevice* device) {
    if (!device)
        return;
    std::lock_guard<std::mutex> lock(g_DeviceMutex);
    if (device->renderer) {
        // Consume an acquired image semaphore even if the caller abandons an open frame.
        // A queue-idle wait alone does not wait for an unconsumed image-acquisition signal.
        if (device->frameOpen && !ResetRequested())
            EndFrame(device);
        if (device->graphicsQueue)
            waitQueueIdle(device->graphicsQueue);
        RemoveCaptureBuffer(device);
        ShutdownScene(device);
        Shutdown2D(device);
        for (auto* semaphore : device->imageAcquired)
            exitSemaphore(device->renderer, semaphore);
        if (device->cmdRing.mPoolCount)
            exitGpuCmdRing(device->renderer, &device->cmdRing);
        if (device->swapChain)
            RemoveSwapChain(device);
        if (device->graphicsQueue)
            exitQueue(device->renderer, device->graphicsQueue);
        if (--g_RendererUsers == 0) {
#if defined(DIRECT3D12) || defined(VULKAN)
            if (g_SharedRootSignatures)
                exitRootSignature(device->renderer);
#endif
            exitResourceLoaderInterface(device->renderer);
            exitRenderer(device->renderer);
            exitGPUConfiguration();
            g_SharedRenderer = nullptr;
            g_SharedRootSignatures = false;
        }
    }
    delete device;
    ReleasePlatform();
}

LuxForgeDevice* Create(const LuxForgeInitDesc* desc, char* error, size_t errorSize) {
    if (!desc || (!desc->nativeWindow && !desc->nativeWindowId)) {
        CopyText(error, errorSize, "No native window");
        return nullptr;
    }
    std::unique_lock<std::mutex> lock(g_DeviceMutex);
    if (!g_SharedRenderer)
        g_ResetRequested.store(false);
    std::string reason;
    if (!AcquirePlatform(reason)) {
        CopyText(error, errorSize, reason);
        return nullptr;
    }
    auto* device = new LuxForgeDevice();
    device->framesInFlight = desc->framesInFlight ? desc->framesInFlight : 2;
    if (device->framesInFlight > 8)
        device->framesInFlight = 8;
    // The shared Forge renderer enables validation at build time. Report the actual mode,
    // rather than pretending a per-viewport request can change that process-wide setting.
#if defined(ENABLE_GRAPHICS_VALIDATION)
    device->validation = true;
#else
    device->validation = false;
#endif

    if (!g_SharedRenderer) {
        RendererDesc settings = {};
        settings.mEnableGpuBasedValidation = false;
        initGPUConfiguration(settings.pExtendedSettings);
        {
            std::lock_guard<std::mutex> logLock(g_InitializationLogMutex);
            g_InitializationError.clear();
            g_CaptureInitializationError = true;
        }
        initRenderer("LuxEngine", &settings, &g_SharedRenderer);
        {
            std::lock_guard<std::mutex> logLock(g_InitializationLogMutex);
            g_CaptureInitializationError = false;
            reason = g_InitializationError;
        }
        if (!g_SharedRenderer) {
            exitGPUConfiguration();
            delete device;
            ReleasePlatform();
            CopyText(error, errorSize,
                     reason.empty() ? "The Forge renderer failed to start" : reason);
            return nullptr;
        }
        setupGPUConfigurationPlatformParameters(g_SharedRenderer, settings.pExtendedSettings);
        initResourceLoaderInterface(g_SharedRenderer);
#if defined(DIRECT3D12) || defined(VULKAN)
        RootSignatureDesc signatures = {};
        INIT_RS_DESC(signatures, "default.rootsig", "compute.rootsig");
        initRootSignature(g_SharedRenderer, &signatures);
        g_SharedRootSignatures = true;
#endif
    }
    device->renderer = g_SharedRenderer;
    ++g_RendererUsers;
    QueueDesc queue = {};
    queue.mType = QUEUE_TYPE_GRAPHICS;
    initQueue(device->renderer, &queue, &device->graphicsQueue);
#if defined(_WIN32)
    device->window.type = WINDOW_HANDLE_TYPE_WIN32;
    device->window.window = static_cast<HWND>(desc->nativeWindow);
#elif defined(__APPLE__)
    device->window.window = desc->nativeWindow;
#else
    if (desc->windowType == LUX_FORGE_WINDOW_WAYLAND) {
        device->window.type = WINDOW_HANDLE_TYPE_WAYLAND;
        device->window.wl_display = static_cast<wl_display*>(desc->nativeDisplay);
        device->window.wl_surface = static_cast<wl_surface*>(desc->nativeWindow);
    } else {
        device->window.type = WINDOW_HANDLE_TYPE_XLIB;
        device->window.display = static_cast<Display*>(desc->nativeDisplay);
        device->window.window = static_cast<Window>(desc->nativeWindowId);
    }
#endif
    device->width = desc->width ? desc->width : 1;
    device->height = desc->height ? desc->height : 1;
    device->vsync = desc->vsync != 0;
    if (!AddSwapChain(device)) {
        lock.unlock();
        Destroy(device);
        CopyText(error, errorSize, "The Forge could not create a swap chain for the window");
        return nullptr;
    }
    GpuCmdRingDesc ring = {};
    ring.pQueue = device->graphicsQueue;
    ring.mPoolCount = device->framesInFlight;
    ring.mCmdPerPoolCount = 1;
    ring.mAddSyncPrimitives = true;
    initGpuCmdRing(device->renderer, &ring, &device->cmdRing);
    device->imageAcquired.resize(device->framesInFlight);
    for (auto& semaphore : device->imageAcquired)
        initSemaphore(device->renderer, &semaphore);
    if (desc->maxFrameVertexBytes)
        device->maxFrameVertexBytes = desc->maxFrameVertexBytes;
    std::string setupError;
    DeviceAlive(device, "after swap chain");
    if (!AddDepthTarget(device))
        setupError = "Depth buffer could not be created";
    else if (DeviceAlive(device, "after depth target"))
        Init2D(device, setupError);
    if (!setupError.empty()) {
        lock.unlock();
        Destroy(device);
        CopyText(error, errorSize, setupError);
        return nullptr;
    }
    return device;
}

int BeginFrame(LuxForgeDevice* device) {
    if (!device || !device->swapChain)
        return LUX_FORGE_FRAME_ERROR;
    if (ResetRequested())
        return Fail(device, "GPU device was lost"), LUX_FORGE_FRAME_ERROR;
    if (device->frameOpen)
        return Fail(device, "BeginFrame called twice"), LUX_FORGE_FRAME_ERROR;
#if defined(_WIN32)
    if (IsIconic(device->window.window))
        return LUX_FORGE_FRAME_SKIPPED;
#endif

    device->element = getNextGpuCmdRingElement(&device->cmdRing, true, 1);
    FenceStatus status = FENCE_STATUS_COMPLETE;
    getFenceStatus(device->renderer, device->element.pFence, &status);
    if (status == FENCE_STATUS_INCOMPLETE)
        waitForFences(device->renderer, 1, &device->element.pFence);
    resetCmdPool(device->renderer, device->element.pCmdPool);
    device->frameSlot = device->cmdRing.mPoolIndex;
    if (device->slotSubmitted[device->frameSlot] > device->completedFrames)
        device->completedFrames = device->slotSubmitted[device->frameSlot];
    {
        // This slot's previous frame is finished: its per-frame memory can be reused.
        auto& frame = device->frames[device->frameSlot];
        for (Buffer* buffer : frame.retired)
            removeResource(buffer);
        frame.retired.clear();
        frame.vertexUsed = 0;
        frame.draws = 0;
    }
    // Block 0 of this frame: no lighting until SetLighting (zeroes, like a fresh Vulkan frame).
    if (device->lightBuffers[device->frameSlot])
        std::memset(device->lightBuffers[device->frameSlot]->pCpuMappedAddress, 0,
                    LuxForgeDevice::kLightStride);
    device->lightBlocksUsed = 1;
    device->currentLight = 0;
    CollectGarbage(device, false);
    BeginSceneFrame(device);
    device->frameStats.drawCalls = 0;
    device->frameStats.pipelineBinds = 0;
    device->frameStats.vertexBytes = 0;

    uint32_t index = UINT32_MAX;
    acquireNextImage(device->renderer, device->swapChain, device->imageAcquired[device->frameSlot],
                     nullptr, &index);
    if (index == UINT32_MAX) {
        // Swap chain is out of date; rebuild it at the current size and skip this frame.
        waitQueueIdle(device->graphicsQueue);
        RemoveSwapChain(device);
        device->swapChain = nullptr;
        if (!AddSwapChain(device))
            return Fail(device, "Swap chain could not be recreated"), LUX_FORGE_FRAME_ERROR;
        return LUX_FORGE_FRAME_SKIPPED;
    }
    device->imageIndex = index;

    Cmd* cmd = device->element.pCmds[0];
    beginCmd(cmd);
    RenderTarget* target = device->swapChain->ppRenderTargets[index];
    RenderTargetBarrier barrier = {target, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET};
    cmdResourceBarrier(cmd, 0, nullptr, 0, nullptr, 1, &barrier);
    device->frameOpen = true;
    device->passOpen = false;
    return LUX_FORGE_FRAME_READY;
}

int Clear(LuxForgeDevice* device, float r, float g, float b, float a) {
    if (!device || !device->frameOpen)
        return device ? Fail(device, "Clear outside a frame") : 0;
    const float color[4] = {r, g, b, a};
    BindPass(device, true, color);
    return 1;
}

bool PrepareCaptureBuffer(LuxForgeDevice* device, uint32_t width, uint32_t height) {
#if defined(DIRECT3D12)
    const uint32_t rowPitch = (width * 4 + 255u) & ~255u;
#else
    const uint32_t rowPitch = width * 4;
#endif
    if (device->captureBuffer && device->captureWidth == width && device->captureHeight == height)
        return true;
    RemoveCaptureBuffer(device);
    BufferLoadDesc load = {};
    load.ppBuffer = &device->captureBuffer;
    load.mDesc.mSize = static_cast<uint64_t>(rowPitch) * height;
    load.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_TO_CPU;
    load.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    load.mDesc.mStartState = RESOURCE_STATE_COPY_DEST;
    load.mDesc.pName = "LuxForge frame capture";
    addResource(&load, nullptr);
    waitForAllResourceLoads();
    if (!device->captureBuffer || !device->captureBuffer->pCpuMappedAddress)
        return false;
    device->captureWidth = width;
    device->captureHeight = height;
    device->captureRowPitch = rowPitch;
    return true;
}

int EndFrame(LuxForgeDevice* device) {
    if (!device || !device->frameOpen)
        return device ? Fail(device, "EndFrame without BeginFrame") : 0;
    EndPass(device);
    device->frameOpen = false;
    Cmd* cmd = device->element.pCmds[0];
    RenderTarget* target = device->swapChain->ppRenderTargets[device->imageIndex];

    const bool capture =
        device->captureRequested && PrepareCaptureBuffer(device, target->mWidth, target->mHeight);
    if (capture) {
        RenderTargetBarrier toCopy = {target, RESOURCE_STATE_RENDER_TARGET,
                                      RESOURCE_STATE_COPY_SOURCE};
        cmdResourceBarrier(cmd, 0, nullptr, 0, nullptr, 1, &toCopy);
        SubresourceDataDesc sub = {};
#if defined(VULKAN) || defined(METAL)
        sub.mRowPitch = device->captureRowPitch;
        sub.mSlicePitch = device->captureRowPitch * device->captureHeight;
#endif
        cmdCopySubresource(cmd, device->captureBuffer, target->pTexture, &sub);
        RenderTargetBarrier toPresent = {target, RESOURCE_STATE_COPY_SOURCE,
                                         RESOURCE_STATE_PRESENT};
        cmdResourceBarrier(cmd, 0, nullptr, 0, nullptr, 1, &toPresent);
    } else {
        RenderTargetBarrier toPresent = {target, RESOURCE_STATE_RENDER_TARGET,
                                         RESOURCE_STATE_PRESENT};
        cmdResourceBarrier(cmd, 0, nullptr, 0, nullptr, 1, &toPresent);
    }
    endCmd(cmd);

    QueueSubmitDesc submit = {};
    submit.mCmdCount = 1;
    submit.ppCmds = &cmd;
    submit.mWaitSemaphoreCount = 1;
    submit.ppWaitSemaphores = &device->imageAcquired[device->frameSlot];
    submit.mSignalSemaphoreCount = 1;
    submit.ppSignalSemaphores = &device->renderFinished[device->imageIndex];
    submit.pSignalFence = device->element.pFence;
    queueSubmit(device->graphicsQueue, &submit);
    device->slotSubmitted[device->frameSlot] = ++device->frameNumber;

    if (capture) {
        // This frame's ring fence; waiting on it later (even after reuse) covers the copy.
        device->captureFence = device->element.pFence;
        device->captureRequested = false;
        device->captureRecorded = true;
    }

    QueuePresentDesc present = {};
    present.pSwapChain = device->swapChain;
    present.mIndex = static_cast<uint8_t>(device->imageIndex);
    present.mWaitSemaphoreCount = 1;
    present.ppWaitSemaphores = &device->renderFinished[device->imageIndex];
    present.mSubmitDone = true;
    queuePresent(device->graphicsQueue, &present);
    if (ResetRequested())
        return Fail(device, "GPU device was lost while presenting");
    ++device->framesPresented;
    return 1;
}

int Resize(LuxForgeDevice* device, uint32_t width, uint32_t height) {
    if (!device || !device->swapChain)
        return 0;
    if (device->frameOpen)
        return Fail(device, "Resize during a frame");
    if (width == 0 || height == 0)
        return 1; // minimized; keep the old swap chain until a real size arrives
    if (width == device->width && height == device->height)
        return 1;
    waitQueueIdle(device->graphicsQueue);
    RemoveSwapChain(device);
    device->swapChain = nullptr;
    device->width = width;
    device->height = height;
    RemoveDepthTarget(device);
    if (!AddSwapChain(device) || !AddDepthTarget(device))
        return Fail(device, "Swap chain could not be resized");
    return 1;
}

int SetVSync(LuxForgeDevice* device, int enabled) {
    if (!device || !device->swapChain)
        return 0;
    if ((enabled != 0) == device->vsync)
        return 1;
    waitQueueIdle(device->graphicsQueue);
    RemoveSwapChain(device);
    device->vsync = enabled != 0;
    return AddSwapChain(device) ? 1 : Fail(device, "VSync change failed");
}

int WaitIdle(LuxForgeDevice* device) {
    if (!device || !device->graphicsQueue)
        return 0;
    waitQueueIdle(device->graphicsQueue);
    device->completedFrames = device->frameNumber;
    if (!device->frameOpen)
        CollectGarbage(device, false);
    return 1;
}

int RequestCapture(LuxForgeDevice* device) {
    if (!device || !device->swapChain)
        return 0;
    device->captureRequested = true;
    return 1;
}

int ReadCapture(LuxForgeDevice* device, uint32_t* width, uint32_t* height, uint8_t* rgba,
                size_t capacity, size_t* size) {
    if (!device || !device->captureRecorded || !device->captureBuffer)
        return device ? Fail(device, "No captured frame") : 0;
    waitForFences(device->renderer, 1, &device->captureFence);
    const size_t needed = static_cast<size_t>(device->captureWidth) * device->captureHeight * 4;
    if (width)
        *width = device->captureWidth;
    if (height)
        *height = device->captureHeight;
    if (size)
        *size = needed;
    if (!rgba || capacity < needed)
        return rgba ? Fail(device, "Capture buffer too small") : 1;
    const auto* source = static_cast<const uint8_t*>(device->captureBuffer->pCpuMappedAddress);
    for (uint32_t y = 0; y < device->captureHeight; ++y) {
        const uint8_t* row = source + static_cast<size_t>(y) * device->captureRowPitch;
        uint8_t* out = rgba + static_cast<size_t>(y) * device->captureWidth * 4;
        for (uint32_t x = 0; x < device->captureWidth; ++x) {
            out[x * 4 + 0] = row[x * 4 + 2]; // BGRA -> RGBA
            out[x * 4 + 1] = row[x * 4 + 1];
            out[x * 4 + 2] = row[x * 4 + 0];
            out[x * 4 + 3] = row[x * 4 + 3];
        }
    }
    device->captureRecorded = false;
    return 1;
}

uint64_t FramesPresented(const LuxForgeDevice* device) {
    return device ? device->framesPresented : 0;
}

uint32_t CreateTexture(LuxForgeDevice* device, uint32_t width, uint32_t height, const void* rgba,
                       int nearest, int srgb, uint32_t mipLevels) {
    if (!device || !device->textureSet || !width || !height || mipLevels == 0 || mipLevels > 15 ||
        (mipLevels > 1 && (std::max(width, height) >> (mipLevels - 1)) == 0))
        return 0;
    const uint32_t id =
        CreateTextureImpl(device, width, height, rgba, nearest != 0, srgb != 0, mipLevels);
    if (id)
        ++device->liveTextures;
    return id;
}

bool IsUserTexture(const LuxForgeDevice* device, uint32_t id) {
    return device && id != 0 && id <= LuxForgeDevice::kMaxTextures && id != device->whiteTexture &&
           device->textures[id].texture != nullptr;
}

void DestroyTexture(LuxForgeDevice* device, uint32_t id) {
    if (!IsUserTexture(device, id))
        return;
    for (const auto& pending : device->pendingTextures)
        if (pending.id == id)
            return; // already scheduled
    // Frames up to the one being recorded may still sample it.
    device->pendingTextures.push_back({device->frameNumber + (device->frameOpen ? 1u : 0u), id});
    --device->liveTextures;
}

int HasTexture(const LuxForgeDevice* device, uint32_t id) {
    if (!IsUserTexture(device, id))
        return 0;
    for (const auto& pending : device->pendingTextures)
        if (pending.id == id)
            return 0;
    return 1;
}

// LuxDraw layout (Shaders/Lux2DResources.h): viewProjection, model, albedo, colorOptions, options.
void WriteDrawConstants(float* out, const float* viewProjection, const float* model,
                        const float* albedo, float srgb, float metallic, float roughness, float pbr,
                        bool nearest) {
    std::memcpy(out, viewProjection, sizeof(float) * 16);
    std::memcpy(out + 16, model, sizeof(float) * 16);
    std::memcpy(out + 32, albedo, sizeof(float) * 4);
    out[36] = srgb, out[37] = metallic, out[38] = roughness, out[39] = pbr;
    out[40] = nearest ? 1.0f : 0.0f, out[41] = out[42] = out[43] = 0.0f;
}

int DrawTriangles(LuxForgeDevice* device, const LuxForgeVertex* vertices, uint32_t vertexCount,
                  const float* viewProjection, uint32_t texture, int depth) {
    if (!device || !device->frameOpen)
        return device ? Fail(device, "DrawTriangles outside a frame") : 0;
    if (!vertices || vertexCount == 0 || vertexCount % 3 != 0 || !viewProjection)
        return Fail(device, "DrawTriangles needs a non-empty triangle list and a matrix");
    if (texture == 0)
        texture = device->whiteTexture;
    else if (!HasTexture(device, texture))
        return Fail(device, "DrawTriangles with an unknown or destroyed texture");
    auto& frame = device->frames[device->frameSlot];
    if (frame.draws >= LuxForgeDevice::kMaxDraws)
        return Fail(device, "Too many draw calls in one frame for the Forge backend");
    const uint64_t bytes = static_cast<uint64_t>(vertexCount) * LuxForgeDevice::kVertexStride;
    if (!EnsureVertexCapacity(device, frame, bytes))
        return 0;
    std::memcpy(static_cast<uint8_t*>(frame.vertices->pCpuMappedAddress) + frame.vertexUsed,
                vertices, static_cast<size_t>(bytes));
    const uint64_t vertexOffset = frame.vertexUsed;
    frame.vertexUsed += bytes;

    float* constants =
        reinterpret_cast<float*>(static_cast<uint8_t*>(frame.uniforms->pCpuMappedAddress) +
                                 static_cast<size_t>(frame.draws) * LuxForgeDevice::kDrawStride);
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static const float ones[4] = {1, 1, 1, 1};
    WriteDrawConstants(constants, viewProjection, identity, ones, 0, 0, 0, 0,
                       device->textures[texture].nearest);

    if (!device->passOpen)
        BindPass(device, false, nullptr);
    Cmd* cmd = device->element.pCmds[0];
    Pipeline* pipeline = depth ? device->pipelineDepth : device->pipeline2D;
    if (pipeline != device->boundPipeline) {
        cmdBindPipeline(cmd, pipeline);
        device->boundPipeline = pipeline;
        ++device->frameStats.pipelineBinds;
    }
    if (texture != device->boundTexture) {
        cmdBindDescriptorSet(cmd, texture, device->textureSet);
        device->boundTexture = texture;
    }
    cmdBindDescriptorSet(cmd, frame.draws, frame.drawSet);
    const uint32_t stride = LuxForgeDevice::kVertexStride;
    cmdBindVertexBuffer(cmd, 1, &frame.vertices, &stride, &vertexOffset);
    cmdDraw(cmd, vertexCount, 0);
    ++frame.draws;
    ++device->frameStats.drawCalls;
    device->frameStats.vertexBytes += bytes;
    return 1;
}

void Stats(const LuxForgeDevice* device, LuxForgeStats* stats) {
    if (!stats)
        return;
    *stats = {};
    if (!device)
        return;
    *stats = device->frameStats;
    stats->liveTextures = device->liveTextures;
    stats->pendingDeletes = static_cast<uint32_t>(device->pendingTextures.size());
    stats->scenePasses = device->scenePasses;
    stats->textureBytes = 0;
    for (uint32_t id = 1; id < device->textures.size(); ++id)
        if (id != device->whiteTexture)
            stats->textureBytes += device->textures[id].bytes;
    stats->sceneBytes = device->sceneBytes;
}

bool IsLiveBuffer(const LuxForgeDevice* device, uint32_t id) {
    return device && id > 0 && id < device->buffers.size() && device->buffers[id].buffer &&
           !device->buffers[id].pendingDelete;
}

uint32_t CreateBuffer(LuxForgeDevice* device, int type, uint32_t sizeBytes) {
    if (!device || !device->renderer)
        return 0;
    if (sizeBytes == 0 || type < LUX_FORGE_BUFFER_VERTEX || type > LUX_FORGE_BUFFER_STORAGE) {
        device->lastError = "Buffer needs a size and a valid type";
        return 0;
    }
    BufferLoadDesc load = {};
    Buffer* buffer = nullptr;
    load.ppBuffer = &buffer;
    load.mDesc.mSize = sizeBytes;
    load.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
    load.mForceReset = true;
    switch (type) {
    case LUX_FORGE_BUFFER_VERTEX:
        load.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
        load.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        break;
    case LUX_FORGE_BUFFER_INDEX:
        load.mDesc.mDescriptors = DESCRIPTOR_TYPE_INDEX_BUFFER;
        load.mDesc.mStartState = RESOURCE_STATE_INDEX_BUFFER;
        break;
    default: // uniform/storage: no whole-buffer view (CBVs are limited to 64 KiB)
        load.mDesc.mDescriptors = UniformBufferDescriptor;
        load.mDesc.mStartState = RESOURCE_STATE_COMMON;
        break;
    }
    load.mDesc.pName = "LuxForge buffer";
    addResource(&load, nullptr);
    waitForAllResourceLoads();
    FlushResourceUpdateDesc flush = {};
    flushResourceUpdates(&flush);
    if (flush.pOutFence)
        waitForFences(device->renderer, 1, &flush.pOutFence);
    if (!buffer) {
        device->lastError = "Buffer allocation failed";
        return 0;
    }
    uint32_t id;
    if (!device->freeBufferIds.empty()) {
        id = device->freeBufferIds.back();
        device->freeBufferIds.pop_back();
    } else {
        id = static_cast<uint32_t>(device->buffers.size());
        device->buffers.emplace_back();
    }
    auto& slot = device->buffers[id];
    slot = {};
    slot.buffer = buffer;
    slot.size = sizeBytes;
    slot.type = type;
    return id;
}

int UpdateBuffer(LuxForgeDevice* device, uint32_t id, const void* data, uint32_t sizeBytes,
                 uint32_t offsetBytes) {
    if (!IsLiveBuffer(device, id))
        return device ? Fail(device, "UpdateBuffer on an unknown buffer") : 0;
    auto& slot = device->buffers[id];
    if (!data || sizeBytes == 0 || static_cast<uint64_t>(offsetBytes) + sizeBytes > slot.size)
        return Fail(device, "UpdateBuffer range is outside the buffer");
    // Frames still reading the buffer must finish before the copy changes it.
    // Like the Vulkan backend, an update made during a frame is visible to that whole frame
    // (the copy runs before the frame is submitted); only already submitted frames are waited for.
    const uint64_t lastSubmittedUse =
        slot.lastUsedFrame < device->frameNumber ? slot.lastUsedFrame : device->frameNumber;
    if (lastSubmittedUse > device->completedFrames) {
        waitQueueIdle(device->graphicsQueue);
        device->completedFrames = device->frameNumber;
    }
    BufferUpdateDesc update = {};
    update.pBuffer = slot.buffer;
    update.mDstOffset = offsetBytes;
    update.mSize = sizeBytes;
    update.mCurrentState = slot.type == LUX_FORGE_BUFFER_INDEX ? RESOURCE_STATE_INDEX_BUFFER
                           : slot.type == LUX_FORGE_BUFFER_VERTEX
                               ? RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
                               : RESOURCE_STATE_COMMON;
    beginUpdateResource(&update);
    if (!update.pMappedData)
        return Fail(device, "UpdateBuffer could not map upload memory");
    std::memcpy(update.pMappedData, data, sizeBytes);
    endUpdateResource(&update);
    FlushResourceUpdateDesc flush = {};
    flushResourceUpdates(&flush);
    if (flush.pOutFence)
        waitForFences(device->renderer, 1, &flush.pOutFence);
    return 1;
}

void DestroyBuffer(LuxForgeDevice* device, uint32_t id) {
    if (!IsLiveBuffer(device, id))
        return;
    device->buffers[id].pendingDelete = true;
    device->pendingBuffers.push_back({device->frameNumber + (device->frameOpen ? 1u : 0u), id});
}

int HasBuffer(const LuxForgeDevice* device, uint32_t id) {
    return IsLiveBuffer(device, id) ? 1 : 0;
}

int SetLighting(LuxForgeDevice* device, const LuxForgeLighting* lighting) {
    if (!device || !device->frameOpen)
        return device ? Fail(device, "SetLighting outside a frame") : 0;
    if (!lighting)
        return Fail(device, "SetLighting needs lighting");
    if (device->lightBlocksUsed >= LuxForgeDevice::kMaxLightBlocks)
        return Fail(device, "Too many SetLighting calls in one frame for the Forge backend");
    const uint32_t block = device->lightBlocksUsed++;
    float* out = reinterpret_cast<float*>(
        static_cast<uint8_t*>(device->lightBuffers[device->frameSlot]->pCpuMappedAddress) +
        static_cast<size_t>(block) * LuxForgeDevice::kLightStride);
    std::memset(out, 0, 576);
    // LuxLights layout: direction, colorIntensity, count (uint4), camera, positions[16],
    // colours[16].
    float dx = lighting->direction[0], dy = lighting->direction[1], dz = lighting->direction[2];
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length > 0)
        dx /= length, dy /= length, dz /= length;
    out[0] = dx, out[1] = dy, out[2] = dz;
    out[4] = lighting->color[0], out[5] = lighting->color[1], out[6] = lighting->color[2],
    out[7] = lighting->intensity;
    const uint32_t count =
        lighting->points ? (lighting->pointCount < 16 ? lighting->pointCount : 16) : 0;
    reinterpret_cast<uint32_t*>(out)[8] = count;
    out[12] = lighting->camera[0], out[13] = lighting->camera[1], out[14] = lighting->camera[2];
    out[15] = lighting->pbr ? 1.0f : 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& p = lighting->points[i];
        float* position = out + 16 + i * 4;
        float* colour = out + 16 + 64 + i * 4;
        position[0] = p.position[0], position[1] = p.position[1], position[2] = p.position[2],
        position[3] = p.radius;
        colour[0] = p.color[0], colour[1] = p.color[1], colour[2] = p.color[2],
        colour[3] = p.intensity;
    }
    device->currentLight = block;
    return 1;
}

int DrawIndexed(LuxForgeDevice* device, uint32_t vertexBuffer, uint32_t indexBuffer,
                uint32_t indexCount, const float* viewProjection, const float* model,
                const float* albedo, uint32_t texture, float metallic, float roughness, int pbr) {
    if (!device || !device->frameOpen)
        return device ? Fail(device, "DrawIndexed outside a frame") : 0;
    if (!IsLiveBuffer(device, vertexBuffer) || !IsLiveBuffer(device, indexBuffer) ||
        device->buffers[vertexBuffer].type != LUX_FORGE_BUFFER_VERTEX ||
        device->buffers[indexBuffer].type != LUX_FORGE_BUFFER_INDEX)
        return Fail(device, "DrawIndexed needs a live vertex buffer and index buffer");
    if (indexCount == 0 || indexCount % 3 != 0 ||
        static_cast<uint64_t>(indexCount) * 4 > device->buffers[indexBuffer].size ||
        !viewProjection || !model || !albedo)
        return Fail(device, "DrawIndexed arguments are invalid");
    const uint32_t textureId = texture ? texture : device->whiteTexture;
    if (texture && !HasTexture(device, texture))
        return Fail(device, "DrawIndexed with an unknown or destroyed texture");
    auto& frame = device->frames[device->frameSlot];
    if (frame.draws >= LuxForgeDevice::kMaxDraws)
        return Fail(device, "Too many draw calls in one frame for the Forge backend");
    float* constants =
        reinterpret_cast<float*>(static_cast<uint8_t*>(frame.uniforms->pCpuMappedAddress) +
                                 static_cast<size_t>(frame.draws) * LuxForgeDevice::kDrawStride);
    const float metal = std::isfinite(metallic) ? (metallic < 0   ? 0
                                                   : metallic > 1 ? 1
                                                                  : metallic)
                                                : 0.0f;
    const float rough = std::isfinite(roughness) ? (roughness < .045f ? .045f
                                                    : roughness > 1   ? 1
                                                                      : roughness)
                                                 : .5f;
    WriteDrawConstants(constants, viewProjection, model, albedo,
                       texture && device->textures[textureId].srgb ? 1.0f : 0.0f, metal, rough,
                       pbr ? 1.0f : 0.0f, device->textures[textureId].nearest);
    if (!device->passOpen)
        BindPass(device, false, nullptr);
    Cmd* cmd = device->element.pCmds[0];
    if (device->pipelineMesh != device->boundPipeline) {
        cmdBindPipeline(cmd, device->pipelineMesh);
        device->boundPipeline = device->pipelineMesh;
        ++device->frameStats.pipelineBinds;
    }
    if (textureId != device->boundTexture) {
        cmdBindDescriptorSet(cmd, textureId, device->textureSet);
        device->boundTexture = textureId;
    }
    cmdBindDescriptorSet(cmd, frame.draws, frame.drawSet);
    cmdBindDescriptorSet(cmd,
                         device->frameSlot * LuxForgeDevice::kMaxLightBlocks + device->currentLight,
                         device->lightSet);
    Buffer* vertices = device->buffers[vertexBuffer].buffer;
    const uint32_t stride = LuxForgeDevice::kVertexStride;
    const uint64_t offset = 0;
    cmdBindVertexBuffer(cmd, 1, &vertices, &stride, &offset);
    cmdBindIndexBuffer(cmd, device->buffers[indexBuffer].buffer, INDEX_TYPE_UINT32, 0);
    cmdDrawIndexed(cmd, indexCount, 0, 0);
    const uint64_t thisFrame = device->frameNumber + 1;
    device->buffers[vertexBuffer].lastUsedFrame = thisFrame;
    device->buffers[indexBuffer].lastUsedFrame = thisFrame;
    ++frame.draws;
    ++device->frameStats.drawCalls;
    return 1;
}

int DeviceLost(const LuxForgeDevice*) {
    return ResetRequested() ? 1 : 0;
}

void Size(const LuxForgeDevice* device, uint32_t* width, uint32_t* height) {
    if (width)
        *width = device ? device->width : 0;
    if (height)
        *height = device ? device->height : 0;
}

void GetInfo(const LuxForgeDevice* device, LuxForgeInfo* info) {
    if (!info)
        return;
    std::memset(info, 0, sizeof(*info));
#if defined(DIRECT3D12)
    std::snprintf(info->apiName, sizeof(info->apiName), "Direct3D 12");
#elif defined(METAL)
    std::snprintf(info->apiName, sizeof(info->apiName), "Metal");
#else
    std::snprintf(info->apiName, sizeof(info->apiName), "Vulkan");
#endif
    if (!device || !device->renderer)
        return;
    std::snprintf(info->deviceName, sizeof(info->deviceName), "%s",
                  device->renderer->pGpu->mGpuVendorPreset.mGpuName);
    info->framesInFlight = device->framesInFlight;
    info->validation = device->validation ? 1 : 0;
}

const char* LastError(const LuxForgeDevice* device) {
    return device ? device->lastError.c_str() : "";
}

// ---------------------------------------------------------------------------------------------
// HDR scene renderer: port of Engine/Renderer/Vulkan/VulkanScene3D.cpp. Each renderScene call
// draws the shadow map, then sky and lit meshes into an RGBA16F target, then tone maps that
// target onto the swap chain.

constexpr TinyImageFormat kHdrFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
constexpr TinyImageFormat kDepthFormat = TinyImageFormat_D32_SFLOAT;

void Transition(Cmd* cmd, LuxForgeDevice::SceneTarget& target, ResourceState state) {
    if (target.state == state)
        return;
    RenderTargetBarrier barrier = {target.target, target.state, state};
    cmdResourceBarrier(cmd, 0, nullptr, 0, nullptr, 1, &barrier);
    target.state = state;
}

// The target is released once every frame up to its last use has finished.
void RetireTarget(LuxForgeDevice* device, LuxForgeDevice::SceneTarget& target) {
    if (!target.target)
        return;
    device->pendingTargets.push_back({target.lastUsedFrame, target.target});
    device->sceneBytes -= target.bytes;
    target = {};
}

bool AddSceneTarget(LuxForgeDevice* device, LuxForgeDevice::SceneTarget& target, uint32_t width,
                    uint32_t height, TinyImageFormat format, uint32_t bytesPerPixel,
                    const char* name) {
    const bool depth = TinyImageFormat_HasDepth(format);
    RenderTargetDesc desc = {};
    desc.mWidth = width;
    desc.mHeight = height;
    desc.mDepth = 1;
    desc.mArraySize = 1;
    desc.mMipLevels = 1;
    desc.mSampleCount = SAMPLE_COUNT_1;
    desc.mFormat = format;
    desc.mFlags = TEXTURE_CREATION_FLAG_FORCE_2D; // a height of 1 would otherwise make a 1D texture
    desc.mStartState = depth ? RESOURCE_STATE_DEPTH_WRITE : RESOURCE_STATE_RENDER_TARGET;
    if (depth)
        desc.mClearValue.depth = 1.0f, desc.mClearValue.stencil = 0;
    desc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
    desc.pName = name;
    addRenderTarget(device->renderer, &desc, &target.target);
    if (!target.target)
        return false;
    target.state = desc.mStartState;
    target.lastUsedFrame = 0;
    target.bytes = static_cast<uint64_t>(width) * height * bytesPerPixel;
    device->sceneBytes += target.bytes;
    return true;
}

void ReleaseEnvironment(LuxForgeDevice* device, uint32_t id) {
    auto& slot = device->environments[id];
    for (Texture* map : slot.maps)
        if (map)
            removeResource(map);
    device->sceneBytes -= slot.bytes;
    slot = {};
    device->freeEnvironmentIds.push_back(id);
}

void CollectSceneGarbage(LuxForgeDevice* device, bool everything) {
    size_t kept = 0;
    for (const auto& pending : device->pendingTargets) {
        if (everything || pending.frame <= device->completedFrames)
            removeRenderTarget(device->renderer, pending.target);
        else
            device->pendingTargets[kept++] = pending;
    }
    device->pendingTargets.resize(kept);
    kept = 0;
    for (const auto& pending : device->pendingEnvironments) {
        if (everything || pending.frame <= device->completedFrames)
            ReleaseEnvironment(device, pending.id);
        else
            device->pendingEnvironments[kept++] = pending;
    }
    device->pendingEnvironments.resize(kept);
}

// Start of a frame: release targets the scene no longer needs. The shadow map goes as soon as a
// frame rendered the scene with shadows off (issue #36: turning shadows off must free it).
void BeginSceneFrame(LuxForgeDevice* device) {
    device->scenePasses = 0;
    const uint64_t last = device->frameNumber; // last submitted frame
    if (device->shadow.target && device->shadow.lastUsedFrame < last &&
        (device->lastShadowlessFrame == last ||
         last - device->shadow.lastUsedFrame >= LuxForgeDevice::kSceneIdleFrames)) {
        RetireTarget(device, device->shadow);
        device->shadowResolution = 0;
    }
    if (device->hdr.target &&
        last - device->hdr.lastUsedFrame >= LuxForgeDevice::kSceneIdleFrames) {
        RetireTarget(device, device->hdr);
        RetireTarget(device, device->sceneDepth);
    }
}

void MeshVertexLayout(VertexLayout& layout) {
    layout = {};
    layout.mBindingCount = 1;
    layout.mBindings[0].mStride = LuxForgeDevice::kVertexStride;
    layout.mAttribCount = 4;
    const ShaderSemantic semantics[4] = {SEMANTIC_POSITION, SEMANTIC_COLOR, SEMANTIC_TEXCOORD0,
                                         SEMANTIC_NORMAL};
    const TinyImageFormat formats[4] = {
        TinyImageFormat_R32G32B32_SFLOAT, TinyImageFormat_R32G32B32A32_SFLOAT,
        TinyImageFormat_R32G32_SFLOAT, TinyImageFormat_R32G32B32_SFLOAT};
    const uint32_t offsets[4] = {offsetof(LuxForgeVertex, position),
                                 offsetof(LuxForgeVertex, color), offsetof(LuxForgeVertex, uv),
                                 offsetof(LuxForgeVertex, normal)};
    for (uint32_t i = 0; i < 4; ++i) {
        layout.mAttribs[i].mSemantic = semantics[i];
        layout.mAttribs[i].mFormat = formats[i];
        layout.mAttribs[i].mBinding = 0;
        layout.mAttribs[i].mLocation = i;
        layout.mAttribs[i].mOffset = offsets[i];
    }
}

Shader* LoadShader(LuxForgeDevice* device, const char* vertex, const char* fragment) {
    ShaderLoadDesc load = {};
    load.mVert.pFileName = vertex;
    load.mFrag.pFileName = fragment;
    Shader* shader = nullptr;
    addShader(device->renderer, &load, &shader);
    return shader;
}

// Shaders, pipelines, descriptor set and constant buffers; created on first use. A failure is
// remembered so later frames fail fast (the engine then falls back to the plain mesh path).
bool InitScene(LuxForgeDevice* device) {
    if (device->pipelineTone)
        return true;
    if (device->sceneInitAttempted)
        return Fail(device, device->sceneInitError.c_str());
    device->sceneInitAttempted = true;
    const auto failed = [device](const char* reason) {
        device->sceneInitError = reason;
        return Fail(device, reason);
    };
    device->shaderScene = LoadShader(device, "luxscene.vert", "luxscene.frag");
    device->shaderShadow = LoadShader(device, "luxshadow.vert", "luxshadow.frag");
    device->shaderSky = LoadShader(device, "luxfullscreen.vert", "luxsky.frag");
    device->shaderTone = LoadShader(device, "luxfullscreen.vert", "luxtone.frag");
    if (!device->shaderScene || !device->shaderShadow || !device->shaderSky || !device->shaderTone)
        return failed("LuxForge scene shaders (DIRECT3D12/luxscene.*, luxshadow.*, "
                      "luxfullscreen.vert, luxsky.frag, "
                      "luxtone.frag) are missing next to LuxForge.dll");

    const uint32_t entries = device->framesInFlight * LuxForgeDevice::kMaxScenePasses * 2;
    DescriptorSetDesc setDesc = SRT_SET_DESC(LuxSceneSrt, Persistent, entries, 0);
    addDescriptorSet(device->renderer, &setDesc, &device->sceneSet);
    if (!device->sceneSet)
        return failed("Scene descriptor set could not be created");
    for (uint32_t sampling = 0; sampling < 4; ++sampling) {
        SamplerDesc sampler = {};
        sampler.mMinFilter = FILTER_LINEAR;
        sampler.mMagFilter = FILTER_LINEAR;
        sampler.mMipMapMode = MIPMAP_MODE_LINEAR;
        sampler.mAddressU = sampler.mAddressV = sampler.mAddressW =
            (sampling & 1) ? ADDRESS_MODE_REPEAT : ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.mMaxAnisotropy = (sampling & 2) ? 8.f : 0.f;
#if defined(VULKAN)
        if (!device->renderer->pGpu->mSamplerAnisotropySupported)
            sampler.mMaxAnisotropy = 0.f;
#endif
        addSampler(device->renderer, &sampler, &device->materialSamplers[sampling]);
        if (!device->materialSamplers[sampling])
            return failed("Material sampler could not be created");
    }
    for (uint32_t frame = 0; frame < device->framesInFlight; ++frame) {
        DescriptorSetDesc materialDesc =
            SRT_SET_DESC(LuxSceneSrt, PerBatch, LuxForgeDevice::kMaterialBankSize, 0);
        for (auto& bank : device->materialSets[frame]) {
            addDescriptorSet(device->renderer, &materialDesc, &bank);
            if (!bank)
                return failed("Material descriptors could not be created");
        }
        BufferLoadDesc load = {};
        load.ppBuffer = &device->sceneUniforms[frame];
        load.mDesc.mSize =
            static_cast<uint64_t>(LuxForgeDevice::kMaxScenePasses) * LuxForgeDevice::kSceneStride;
        load.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        load.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        load.mDesc.mDescriptors = UniformBufferDescriptor; // per-pass CBVs below
        load.mDesc.pName = "LuxForge scene constants";
        addResource(&load, nullptr);
        waitForAllResourceLoads();
        Buffer* uniforms = device->sceneUniforms[frame];
        if (!uniforms || !uniforms->pCpuMappedAddress)
            return failed("Scene constant buffer could not be created");
        for (uint32_t pass = 0; pass < LuxForgeDevice::kMaxScenePasses; ++pass) {
            DescriptorDataRange range = {pass * LuxForgeDevice::kSceneStride,
                                         LuxForgeDevice::kSceneStride, 0};
            DescriptorData param = {};
            param.mIndex = SRT_RES_IDX(LuxSceneSrt, Persistent, gScene);
            param.ppBuffers = &uniforms;
            param.pRanges = &range;
            const uint32_t entry = (frame * LuxForgeDevice::kMaxScenePasses + pass) * 2;
            updateDescriptorSet(device->renderer, entry, device->sceneSet, 1, &param);
            updateDescriptorSet(device->renderer, entry + 1, device->sceneSet, 1, &param);
        }
    }

    VertexLayout layout;
    MeshVertexLayout(layout);
    DepthStateDesc noDepth = {};
    DepthStateDesc depthTest = {};
    depthTest.mDepthTest = true;
    depthTest.mDepthWrite = true;
    depthTest.mDepthFunc = CMP_LEQUAL;
    RasterizerStateDesc raster = {};
    raster.mCullMode = CULL_MODE_NONE;
    TinyImageFormat hdrFormat = kHdrFormat;
    TinyImageFormat swapFormat =
        static_cast<TinyImageFormat>(device->swapChain->ppRenderTargets[0]->mFormat);

    PipelineDesc desc = {};
    desc.mType = PIPELINE_TYPE_GRAPHICS;
    PIPELINE_LAYOUT_DESC(
        desc, SRT_LAYOUT_DESC(LuxSceneSrt, Persistent), SRT_LAYOUT_DESC(LuxSceneSrt, PerFrame),
        SRT_LAYOUT_DESC(LuxSceneSrt, PerBatch), SRT_LAYOUT_DESC(LuxSceneSrt, PerDraw));
    GraphicsPipelineDesc& graphics = desc.mGraphicsDesc;
    graphics.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
    graphics.mSampleCount = SAMPLE_COUNT_1;
    graphics.mDepthStencilFormat = kDepthFormat;
    graphics.pRasterizerState = &raster;
    graphics.pBlendState = nullptr;

    graphics.mRenderTargetCount = 1;
    graphics.pColorFormats = &hdrFormat;
    graphics.pShaderProgram = device->shaderScene;
    graphics.pVertexLayout = &layout;
    graphics.pDepthState = &depthTest;
    addPipeline(device->renderer, &desc, &device->pipelineScene);

    BlendStateDesc materialBlend = {};
    materialBlend.mSrcFactors[0] = BC_SRC_ALPHA;
    materialBlend.mDstFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
    materialBlend.mBlendModes[0] = BM_ADD;
    materialBlend.mSrcAlphaFactors[0] = BC_ONE;
    materialBlend.mDstAlphaFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
    materialBlend.mBlendAlphaModes[0] = BM_ADD;
    materialBlend.mColorWriteMasks[0] = COLOR_MASK_ALL;
    materialBlend.mRenderTargetMask = BLEND_STATE_TARGET_0;
    for (uint32_t mode = 0; mode < 3; ++mode) {
        for (uint32_t sided = 0; sided < 2; ++sided) {
            depthTest.mDepthWrite = mode != 2;
            raster.mCullMode = sided ? CULL_MODE_NONE : CULL_MODE_BACK;
            graphics.pBlendState = mode == 2 ? &materialBlend : nullptr;
            addPipeline(device->renderer, &desc, &device->materialPipelines[mode * 2 + sided]);
            if (!device->materialPipelines[mode * 2 + sided])
                return failed("Material pipeline could not be created");
        }
    }
    raster.mCullMode = CULL_MODE_NONE;
    depthTest.mDepthWrite = true;
    graphics.pBlendState = nullptr;

    graphics.pShaderProgram = device->shaderSky;
    graphics.pVertexLayout = nullptr;
    graphics.pDepthState = &noDepth;
    addPipeline(device->renderer, &desc, &device->pipelineSky);

    graphics.mRenderTargetCount = 0;
    graphics.pColorFormats = nullptr;
    graphics.pShaderProgram = device->shaderShadow;
    graphics.pVertexLayout = &layout;
    graphics.pDepthState = &depthTest;
    addPipeline(device->renderer, &desc, &device->pipelineShadow);

    graphics.mRenderTargetCount = 1;
    graphics.pColorFormats = &swapFormat;
    graphics.pBlendState = &materialBlend;
    graphics.pShaderProgram = device->shaderTone;
    graphics.pVertexLayout = nullptr;
    graphics.pDepthState = &noDepth;
    addPipeline(device->renderer, &desc, &device->pipelineTone);
    if (!device->pipelineScene || !device->pipelineSky || !device->pipelineShadow ||
        !device->pipelineTone) {
        // pipelineTone doubles as "initialised": make sure it is unset after a partial failure.
        if (device->pipelineTone)
            removePipeline(device->renderer, device->pipelineTone);
        device->pipelineTone = nullptr;
        return failed("Scene pipelines could not be created");
    }
    for (uint32_t id = 255; id >= 1; --id)
        device->freeEnvironmentIds.push_back(id);
    return true;
}

void ShutdownScene(LuxForgeDevice* device) {
    for (auto& frame : device->materialSets)
        for (auto*& bank : frame) {
            if (bank)
                removeDescriptorSet(device->renderer, bank);
            bank = nullptr;
        }
    for (auto*& sampler : device->materialSamplers) {
        if (sampler)
            removeSampler(device->renderer, sampler);
        sampler = nullptr;
    }
    for (size_t id = 1; id < device->environments.size(); ++id)
        if (device->environments[id].maps[0] || device->environments[id].maps[3])
            ReleaseEnvironment(device, static_cast<uint32_t>(id));
    device->pendingEnvironments.clear();
    RetireTarget(device, device->hdr);
    RetireTarget(device, device->sceneDepth);
    RetireTarget(device, device->shadow);
    for (const auto& pending : device->pendingTargets)
        removeRenderTarget(device->renderer, pending.target);
    device->pendingTargets.clear();
    for (Buffer*& uniforms : device->sceneUniforms)
        if (uniforms) {
            removeResource(uniforms);
            uniforms = nullptr;
        }
    if (device->sceneSet)
        removeDescriptorSet(device->renderer, device->sceneSet);
    device->sceneSet = nullptr;
    for (Pipeline*& pipeline : device->materialPipelines) {
        if (pipeline)
            removePipeline(device->renderer, pipeline);
        pipeline = nullptr;
    }
    for (Pipeline** pipeline : {&device->pipelineScene, &device->pipelineShadow,
                                &device->pipelineSky, &device->pipelineTone})
        if (*pipeline) {
            removePipeline(device->renderer, *pipeline);
            *pipeline = nullptr;
        }
    for (Shader** shader :
         {&device->shaderScene, &device->shaderShadow, &device->shaderSky, &device->shaderTone})
        if (*shader) {
            removeShader(device->renderer, *shader);
            *shader = nullptr;
        }
}

bool IsLiveEnvironment(const LuxForgeDevice* device, uint32_t id) {
    return device && id > 0 && id < device->environments.size() &&
           device->environments[id].maps[0] && !device->environments[id].pendingDelete;
}

Texture* UploadEnvironmentImage(LuxForgeDevice* device, const LuxForgeEnvironmentImage& image,
                                uint64_t& bytes) {
    TextureDesc desc = {};
    desc.mWidth = image.size;
    desc.mHeight = image.size;
    desc.mDepth = 1;
    desc.mArraySize = image.layers;
    desc.mMipLevels = image.levels;
    desc.mSampleCount = SAMPLE_COUNT_1;
    desc.mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    desc.mStartState = RESOURCE_STATE_SHADER_RESOURCE;
    desc.mDescriptors = image.layers == 6 ? DESCRIPTOR_TYPE_TEXTURE_CUBE : DESCRIPTOR_TYPE_TEXTURE;
    // Without this a 1-texel-high image becomes a 1D texture (and samples as zero in 2D shaders).
    desc.mFlags = TEXTURE_CREATION_FLAG_FORCE_2D;
    desc.pName = "LuxForge environment";
    Texture* texture = nullptr;
    TextureLoadDesc load = {};
    load.pDesc = &desc;
    load.ppTexture = &texture;
    addResource(&load, nullptr);
    waitForAllResourceLoads();
    if (!texture)
        return nullptr;
    TextureUpdateDesc update = {
        texture, 0, image.levels, 0, image.layers, RESOURCE_STATE_SHADER_RESOURCE};
    beginUpdateResource(&update);
    uint64_t source = 0; // in floats
    for (uint32_t level = 0; level < image.levels; ++level) {
        const uint32_t side = image.size >> level ? image.size >> level : 1;
        const size_t rowFloats = static_cast<size_t>(side) * 4;
        for (uint32_t layer = 0; layer < image.layers; ++layer) {
            TextureSubresourceUpdate sub = update.getSubresourceUpdateDesc(level, layer);
            for (uint32_t row = 0; row < sub.mRowCount && row < side; ++row)
                std::memcpy(sub.pMappedData + static_cast<size_t>(row) * sub.mDstRowStride,
                            image.pixels + source +
                                (static_cast<size_t>(layer) * side + row) * rowFloats,
                            rowFloats * sizeof(float));
        }
        source += static_cast<uint64_t>(side) * side * image.layers * 4;
    }
    endUpdateResource(&update);
    bytes += source * sizeof(float);
    return texture;
}

uint32_t CreateEnvironment(LuxForgeDevice* device, const LuxForgeEnvironmentImage* images) {
    if (!device || !device->renderer)
        return 0;
    if (!InitScene(device))
        return 0;
    if (!images) {
        device->lastError = "createEnvironment needs four images";
        return 0;
    }
    for (uint32_t i = 0; i < 4; ++i) {
        const auto& image = images[i];
        uint64_t floats = 0;
        bool valid = image.pixels && image.size >= 1 && image.size <= 4096 && image.levels >= 1 &&
                     image.levels <= 13 && (image.size >> (image.levels - 1)) >= 1 &&
                     image.layers == (i < 3 ? 6u : 1u);
        for (uint32_t level = 0; valid && level < image.levels; ++level) {
            const uint64_t side = image.size >> level;
            floats += side * side * image.layers * 4;
        }
        if (!valid || floats != image.floatCount) {
            device->lastError =
                "Environment image has an invalid size, level count, layer count or pixel count";
            return 0;
        }
    }
    if (device->freeEnvironmentIds.empty()) {
        device->lastError = "Too many live environments for the Forge backend";
        return 0;
    }
    LuxForgeDevice::EnvironmentSlot slot;
    for (uint32_t i = 0; i < 4; ++i) {
        slot.maps[i] = UploadEnvironmentImage(device, images[i], slot.bytes);
        if (!slot.maps[i]) {
            for (Texture* map : slot.maps)
                if (map)
                    removeResource(map);
            device->lastError = "Environment texture creation failed";
            return 0;
        }
    }
    FlushResourceUpdateDesc flush = {};
    flushResourceUpdates(&flush);
    if (flush.pOutFence)
        waitForFences(device->renderer, 1, &flush.pOutFence);
    const uint32_t id = device->freeEnvironmentIds.back();
    device->freeEnvironmentIds.pop_back();
    if (id >= device->environments.size())
        device->environments.resize(id + 1);
    device->environments[id] = slot;
    device->sceneBytes += slot.bytes;
    return id;
}

void DestroyEnvironment(LuxForgeDevice* device, uint32_t id) {
    if (!IsLiveEnvironment(device, id))
        return;
    device->environments[id].pendingDelete = true;
    device->pendingEnvironments.push_back(
        {device->frameNumber + (device->frameOpen ? 1u : 0u), id});
}

// Draw resources stay valid for the frame being recorded even after destroyBuffer/destroyTexture
// (their release waits for this frame), so only the slot contents are checked.
bool SceneDrawUsable(const LuxForgeDevice* device, const LuxForgeSceneDraw& draw) {
    for (float value : draw.model)
        if (!std::isfinite(value))
            return false;
    for (float value : draw.albedo)
        if (!std::isfinite(value))
            return false;
    const auto buffer = [device](uint32_t id, int type) -> const LuxForgeDevice::BufferSlot* {
        if (id == 0 || id >= device->buffers.size() || !device->buffers[id].buffer ||
            device->buffers[id].type != type)
            return nullptr;
        return &device->buffers[id];
    };
    for (const auto texture :
         {draw.normalTexture, draw.metallicTexture, draw.roughnessTexture, draw.occlusionTexture})
        if (texture &&
            (texture > LuxForgeDevice::kMaxTextures || !device->textures[texture].texture))
            return false;
    if (draw.sampling > 3 || !std::isfinite(draw.normalScale) || draw.normalScale < 0 ||
        draw.normalScale > 16 || !std::isfinite(draw.occlusionStrength) ||
        draw.occlusionStrength < 0 || draw.occlusionStrength > 1)
        return false;
    const auto* index = buffer(draw.indexBuffer, LUX_FORGE_BUFFER_INDEX);
    return buffer(draw.vertexBuffer, LUX_FORGE_BUFFER_VERTEX) && index && draw.indexCount > 0 &&
           draw.indexCount % 3 == 0 && static_cast<uint64_t>(draw.indexCount) * 4 <= index->size &&
           (draw.texture == 0 || (draw.texture <= LuxForgeDevice::kMaxTextures &&
                                  device->textures[draw.texture].texture));
}

void DrawSceneMeshes(LuxForgeDevice* device, Cmd* cmd,
                     const std::vector<const LuxForgeSceneDraw*>& draws, uint32_t firstDraw,
                     bool shadow = false) {
    const uint32_t stride = LuxForgeDevice::kVertexStride;
    const uint64_t offset = 0;
    for (size_t i = 0; i < draws.size(); ++i) {
        const auto& draw = *draws[i];
        if (shadow && draw.alphaMode == 2)
            continue;
        if (!shadow) {
            cmdBindPipeline(
                cmd, device->materialPipelines[draw.alphaMode * 2 + (draw.doubleSided ? 1 : 0)]);
            ++device->frameStats.pipelineBinds;
        }
        const uint32_t material = firstDraw + static_cast<uint32_t>(i);
        cmdBindDescriptorSet(
            cmd, material % LuxForgeDevice::kMaterialBankSize,
            device->materialSets[device->frameSlot][material / LuxForgeDevice::kMaterialBankSize]);
        cmdBindDescriptorSet(cmd, firstDraw + static_cast<uint32_t>(i),
                             device->frames[device->frameSlot].drawSet);
        Buffer* vertices = device->buffers[draw.vertexBuffer].buffer;
        cmdBindVertexBuffer(cmd, 1, &vertices, &stride, &offset);
        cmdBindIndexBuffer(cmd, device->buffers[draw.indexBuffer].buffer, INDEX_TYPE_UINT32, 0);
        cmdDrawIndexed(cmd, draw.indexCount, 0, 0);
        ++device->frameStats.drawCalls;
    }
}

void BindPipeline(LuxForgeDevice* device, Cmd* cmd, Pipeline* pipeline) {
    cmdBindPipeline(cmd, pipeline);
    ++device->frameStats.pipelineBinds;
}

int RenderScene(LuxForgeDevice* device, const LuxForgeScene* scene, const LuxForgeSceneDraw* draws,
                uint32_t drawCount, uint32_t* drawn) {
    if (drawn)
        *drawn = 0;
    if (!device || !device->frameOpen)
        return device ? Fail(device, "renderScene outside a frame") : 0;
    if (!scene || (drawCount && !draws))
        return Fail(device, "renderScene needs a scene and its draws");
    if (!InitScene(device))
        return 0;
    if (device->scenePasses >= LuxForgeDevice::kMaxScenePasses)
        return Fail(device, "Too many 3D scene passes in one frame");
    if (!IsLiveEnvironment(device, scene->environmentMap))
        return Fail(device, "renderScene with an unknown or destroyed environment");
    const uint32_t resolution = scene->shadowResolution;
    const bool shadows = resolution != 0;
    if (shadows && (resolution < 16 || resolution > 16384))
        return Fail(device, "Shadow resolution must be 16..16384");
    if (device->hdr.target && (device->hdr.target->mWidth != device->width ||
                               device->hdr.target->mHeight != device->height)) {
        RetireTarget(device, device->hdr);
        RetireTarget(device, device->sceneDepth);
    }
    if (!device->hdr.target &&
        (!AddSceneTarget(device, device->hdr, device->width, device->height, kHdrFormat, 8,
                         "LuxForge HDR") ||
         !AddSceneTarget(device, device->sceneDepth, device->width, device->height, kDepthFormat, 4,
                         "LuxForge scene depth"))) {
        RetireTarget(device, device->hdr);
        return Fail(device, "HDR render target could not be created");
    }
    if (shadows && device->shadowResolution != resolution) {
        RetireTarget(device, device->shadow);
        device->shadowResolution = 0;
        if (!AddSceneTarget(device, device->shadow, resolution, resolution, kDepthFormat, 4,
                            "LuxForge shadow map"))
            return Fail(device, "Shadow map could not be created");
        device->shadowResolution = resolution;
    }

    auto& frame = device->frames[device->frameSlot];
    std::vector<const LuxForgeSceneDraw*> visible;
    visible.reserve(drawCount);
    for (uint32_t i = 0; i < drawCount; ++i)
        if (draws[i].alphaMode <= 2 && std::isfinite(draws[i].alphaCutoff) &&
            draws[i].alphaCutoff >= 0 && draws[i].alphaCutoff <= 1 &&
            SceneDrawUsable(device, draws[i]))
            visible.push_back(&draws[i]);
    std::stable_sort(visible.begin(), visible.end(), [scene](const auto* a, const auto* b) {
        if ((a->alphaMode == 2) != (b->alphaMode == 2))
            return a->alphaMode != 2;
        if (a->alphaMode != 2)
            return false;
        const auto distance = [scene](const auto* draw) {
            float sum = 0;
            for (unsigned axis = 0; axis < 3; ++axis) {
                const float delta = draw->model[12 + axis] - scene->camera[axis];
                sum += delta * delta;
            }
            return sum;
        };
        return distance(a) > distance(b);
    });
    const uint32_t budget = LuxForgeDevice::kMaxDraws - frame.draws;
    if (visible.size() > budget)
        visible.resize(budget); // like the Vulkan backend: excess objects are skipped

    const uint64_t thisFrame = device->frameNumber + 1;
    const uint32_t pass = device->scenePasses++;
    if (!shadows)
        device->lastShadowlessFrame = thisFrame;

    // Scene constants (LuxSceneData).
    float* constants = reinterpret_cast<float*>(
        static_cast<uint8_t*>(device->sceneUniforms[device->frameSlot]->pCpuMappedAddress) +
        static_cast<size_t>(pass) * LuxForgeDevice::kSceneStride);
    std::memcpy(constants + 0, scene->camera, sizeof(float) * 4);
    std::memcpy(constants + 4, scene->environment, sizeof(float) * 4);
    std::memcpy(constants + 8, scene->options, sizeof(float) * 4);
    std::memcpy(constants + 12, scene->right, sizeof(float) * 4);
    std::memcpy(constants + 16, scene->up, sizeof(float) * 4);
    std::memcpy(constants + 20, scene->forward, sizeof(float) * 4);
    std::memcpy(constants + 24, scene->shadowMatrix, sizeof(float) * 16);
    std::memcpy(constants + 40, scene->shadowOptions, sizeof(float) * 4);
    if (!shadows)
        constants[40] = 0.0f;

    // Two descriptor entries per pass: the scene passes never see the HDR image they render to
    // (the BRDF table stands in), the tone pass reads it.
    const auto& environment = device->environments[scene->environmentMap];
    Texture* textures[6] = {environment.maps[1],
                            environment.maps[2],
                            environment.maps[0],
                            environment.maps[3],
                            shadows ? device->shadow.target->pTexture : environment.maps[3],
                            environment.maps[3]};
    const uint32_t indices[6] = {SRT_RES_IDX(LuxSceneSrt, Persistent, gDiffuseEnv),
                                 SRT_RES_IDX(LuxSceneSrt, Persistent, gSpecularEnv),
                                 SRT_RES_IDX(LuxSceneSrt, Persistent, gSkyEnv),
                                 SRT_RES_IDX(LuxSceneSrt, Persistent, gBrdfLut),
                                 SRT_RES_IDX(LuxSceneSrt, Persistent, gShadowMap),
                                 SRT_RES_IDX(LuxSceneSrt, Persistent, gHdrImage)};
    DescriptorData params[10] = {};
    for (uint32_t i = 0; i < 6; ++i) {
        params[i].mIndex = indices[i];
        params[i].ppTextures = &textures[i];
    }
    const uint32_t samplerBindings[4] = {
        SRT_RES_IDX(LuxSceneSrt, Persistent, gMaterialClamp),
        SRT_RES_IDX(LuxSceneSrt, Persistent, gMaterialRepeat),
        SRT_RES_IDX(LuxSceneSrt, Persistent, gMaterialAnisotropicClamp),
        SRT_RES_IDX(LuxSceneSrt, Persistent, gMaterialAnisotropicRepeat)};
    for (uint32_t sampler = 0; sampler < 4; ++sampler) {
        params[6 + sampler].mIndex = samplerBindings[sampler];
        params[6 + sampler].ppSamplers = &device->materialSamplers[sampler];
    }
    const uint32_t entry = (device->frameSlot * LuxForgeDevice::kMaxScenePasses + pass) * 2;
    updateDescriptorSet(device->renderer, entry, device->sceneSet, 10, params);
    textures[5] = device->hdr.target->pTexture;
    updateDescriptorSet(device->renderer, entry + 1, device->sceneSet, 10, params);

    // Per-object constants: one LuxDraw block serves the shadow and the HDR pass.
    const uint32_t firstDraw = frame.draws;
    for (size_t i = 0; i < visible.size(); ++i) {
        const auto& draw = *visible[i];
        const uint32_t texture = draw.texture ? draw.texture : device->whiteTexture;
        float* block = reinterpret_cast<float*>(
            static_cast<uint8_t*>(frame.uniforms->pCpuMappedAddress) +
            static_cast<size_t>(firstDraw + i) * LuxForgeDevice::kDrawStride);
        const float metal = std::isfinite(draw.metallic) ? (draw.metallic < 0   ? 0
                                                            : draw.metallic > 1 ? 1
                                                                                : draw.metallic)
                                                         : 0.0f;
        const float rough = std::isfinite(draw.roughness) ? (draw.roughness < .045f ? .045f
                                                             : draw.roughness > 1   ? 1
                                                                                  : draw.roughness)
                                                          : .5f;
        // colorOptions = (1, metallic, roughness, 1): discard transparent texels, PBR lighting.
        WriteDrawConstants(block, scene->viewProjection, draw.model, draw.albedo, 1.0f, metal,
                           rough, draw.legacy ? 0.0f : 1.0f, device->textures[texture].nearest);
        block[41] = static_cast<float>(draw.alphaMode);
        block[42] = draw.alphaCutoff;
        block[43] =
            static_cast<float>((draw.normalTexture ? 1 : 0) | (draw.metallicTexture ? 2 : 0) |
                               (draw.roughnessTexture ? 4 : 0) | (draw.occlusionTexture ? 8 : 0));
        block[44] = draw.normalScale;
        block[45] = draw.occlusionStrength;
        block[46] = static_cast<float>(draw.sampling);
        block[47] = 0.f;
        const uint32_t materialIndex = firstDraw + static_cast<uint32_t>(i);
        const uint32_t materialIds[5] = {draw.texture, draw.normalTexture, draw.metallicTexture,
                                         draw.roughnessTexture, draw.occlusionTexture};
        Texture* materialTextures[5] = {};
        DescriptorData materialParams[5] = {};
        const uint32_t materialBindings[5] = {
            SRT_RES_IDX(LuxSceneSrt, PerBatch, gTexture),
            SRT_RES_IDX(LuxSceneSrt, PerBatch, gNormalTexture),
            SRT_RES_IDX(LuxSceneSrt, PerBatch, gMetallicTexture),
            SRT_RES_IDX(LuxSceneSrt, PerBatch, gRoughnessTexture),
            SRT_RES_IDX(LuxSceneSrt, PerBatch, gOcclusionTexture)};
        for (uint32_t map = 0; map < 5; ++map) {
            materialTextures[map] =
                device->textures[materialIds[map] ? materialIds[map] : device->whiteTexture]
                    .texture;
            materialParams[map].mIndex = materialBindings[map];
            materialParams[map].ppTextures = &materialTextures[map];
        }
        updateDescriptorSet(device->renderer, materialIndex % LuxForgeDevice::kMaterialBankSize,
                            device->materialSets[device->frameSlot]
                                                [materialIndex / LuxForgeDevice::kMaterialBankSize],
                            5, materialParams);
        device->buffers[draw.vertexBuffer].lastUsedFrame = thisFrame;
        device->buffers[draw.indexBuffer].lastUsedFrame = thisFrame;
    }
    frame.draws += static_cast<uint32_t>(visible.size());

    Cmd* cmd = device->element.pCmds[0];
    EndPass(device);
    if (shadows) {
        Transition(cmd, device->shadow, RESOURCE_STATE_DEPTH_WRITE);
        BindRenderTargetsDesc bind = {};
        bind.mDepthStencil.pDepthStencil = device->shadow.target;
        bind.mDepthStencil.mLoadAction = LOAD_ACTION_CLEAR;
        bind.mDepthStencil.mStoreAction = STORE_ACTION_STORE;
        bind.mDepthStencil.mLoadActionStencil = LOAD_ACTION_DONTCARE;
        bind.mDepthStencil.mStoreActionStencil = STORE_ACTION_DONTCARE;
        cmdBindRenderTargets(cmd, &bind);
        cmdSetViewport(cmd, 0.0f, 0.0f, static_cast<float>(resolution),
                       static_cast<float>(resolution), 0.0f, 1.0f);
        cmdSetScissor(cmd, 0, 0, resolution, resolution);
        BindPipeline(device, cmd, device->pipelineShadow);
        cmdBindDescriptorSet(cmd, entry, device->sceneSet);
        DrawSceneMeshes(device, cmd, visible, firstDraw, true);
        cmdBindRenderTargets(cmd, nullptr);
        Transition(cmd, device->shadow, RESOURCE_STATE_SHADER_RESOURCE);
        device->shadow.lastUsedFrame = thisFrame;
    }

    // HDR pass. Alpha 0 marks pixels no 3D geometry touched; the tone pass skips them.
    Transition(cmd, device->hdr, RESOURCE_STATE_RENDER_TARGET);
    {
        BindRenderTargetsDesc bind = {};
        bind.mRenderTargetCount = 1;
        bind.mRenderTargets[0].pRenderTarget = device->hdr.target;
        bind.mRenderTargets[0].mLoadAction = LOAD_ACTION_CLEAR;
        bind.mRenderTargets[0].mStoreAction = STORE_ACTION_STORE;
        bind.mRenderTargets[0].mClearValue = {{0.0f, 0.0f, 0.0f, 0.0f}};
        bind.mRenderTargets[0].mOverrideClearValue = 1;
        bind.mDepthStencil.pDepthStencil = device->sceneDepth.target;
        bind.mDepthStencil.mLoadAction = LOAD_ACTION_CLEAR;
        bind.mDepthStencil.mStoreAction = STORE_ACTION_STORE;
        bind.mDepthStencil.mLoadActionStencil = LOAD_ACTION_DONTCARE;
        bind.mDepthStencil.mStoreActionStencil = STORE_ACTION_DONTCARE;
        cmdBindRenderTargets(cmd, &bind);
    }
    cmdSetViewport(cmd, 0.0f, 0.0f, static_cast<float>(device->width),
                   static_cast<float>(device->height), 0.0f, 1.0f);
    cmdSetScissor(cmd, 0, 0, device->width, device->height);
    if (scene->options[3] > 0.5f) {
        BindPipeline(device, cmd, device->pipelineSky);
        cmdBindDescriptorSet(cmd, entry, device->sceneSet);
        cmdDraw(cmd, 3, 0);
        ++device->frameStats.drawCalls;
    }
    BindPipeline(device, cmd, device->pipelineScene);
    cmdBindDescriptorSet(cmd, entry, device->sceneSet);
    cmdBindDescriptorSet(cmd,
                         device->frameSlot * LuxForgeDevice::kMaxLightBlocks + device->currentLight,
                         device->lightSet);
    DrawSceneMeshes(device, cmd, visible, firstDraw);
    cmdBindRenderTargets(cmd, nullptr);
    Transition(cmd, device->hdr, RESOURCE_STATE_SHADER_RESOURCE);
    device->hdr.lastUsedFrame = device->sceneDepth.lastUsedFrame = thisFrame;

    // Tone mapping onto the swap chain (keeps its contents and the main depth buffer).
    BindPass(device, false, nullptr);
    BindPipeline(device, cmd, device->pipelineTone);
    cmdBindDescriptorSet(cmd, entry + 1, device->sceneSet);
    cmdDraw(cmd, 3, 0);
    ++device->frameStats.drawCalls;
    device->boundPipeline = device->pipelineTone;
    device->boundTexture = 0;
    if (drawn)
        *drawn = static_cast<uint32_t>(visible.size());
    return 1;
}

const LuxForgeApi g_Api = {LUX_FORGE_API_VERSION,
                           Create,
                           Destroy,
                           GetInfo,
                           LastError,
                           BeginFrame,
                           Clear,
                           EndFrame,
                           Resize,
                           SetVSync,
                           WaitIdle,
                           RequestCapture,
                           ReadCapture,
                           FramesPresented,
                           DeviceLost,
                           Size,
                           CreateTexture,
                           DestroyTexture,
                           HasTexture,
                           DrawTriangles,
                           Stats,
                           CreateBuffer,
                           UpdateBuffer,
                           DestroyBuffer,
                           HasBuffer,
                           SetLighting,
                           DrawIndexed,
                           CreateEnvironment,
                           DestroyEnvironment,
                           RenderScene};
} // namespace

extern "C" LUX_FORGE_EXPORT const LuxForgeApi* LuxForgeGetApi(void) {
    return &g_Api;
}
