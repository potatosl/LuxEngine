#pragma once

#include "Vulkan/VulkanFault.hpp"
#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>
#include <array>
#include <atomic>
#include <deque>
#include <string>
#include <string_view>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace LuxEngine::Vk {

constexpr uint32 MaxFramesInFlight = 3;
constexpr uint32 MaxPointLights = 16;
constexpr VkDeviceSize UniformObjectBytes = 96;
constexpr VkDeviceSize UniformLightBytes = 64 + 32 * MaxPointLights;

const char* ResultString(VkResult result);

template <class T> class HandlePool {
public:
    uint64 Insert(T value) {
        uint32 index = 0;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<uint32>(slots_.size());
            slots_.emplace_back();
        }
        Slot& slot = slots_[index];
        slot.value = std::move(value);
        slot.live = true;
        ++live_;
        return (static_cast<uint64>(slot.generation) << 32) | index;
    }
    T* Get(uint64 handle) {
        const auto index = static_cast<uint32>(handle);
        if (index >= slots_.size())
            return nullptr;
        Slot& slot = slots_[index];
        return slot.live && slot.generation == static_cast<uint32>(handle >> 32) ? &slot.value
                                                                                 : nullptr;
    }
    bool Remove(uint64 handle, T& out) {
        T* value = Get(handle);
        if (!value)
            return false;
        out = std::move(*value);
        Kill(static_cast<uint32>(handle));
        return true;
    }
    template <class F> void Drain(F&& destroy) {
        for (uint32 index = 0; index < slots_.size(); ++index)
            if (slots_[index].live) {
                destroy(slots_[index].value);
                Kill(index);
            }
    }
    uint32 Live() const {
        return live_;
    }

private:
    struct Slot {
        T value{};
        uint32 generation = 1;
        bool live = false;
    };
    void Kill(uint32 index) {
        Slot& slot = slots_[index];
        slot.value = {};
        slot.live = false;
        slot.generation = slot.generation == 0xffffffffu ? 1 : slot.generation + 1;
        free_.push_back(index);
        --live_;
    }
    std::vector<Slot> slots_;
    std::vector<uint32> free_;
    uint32 live_ = 0;
};

struct BufferRecord {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkDeviceSize size = 0;
};

struct TextureRecord {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint32 width = 0;
    uint32 height = 0;
};

struct Retired {
    uint64 serial = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDeviceSize stagingBytes = 0;
};

struct VertexRing {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    uint8* mapped = nullptr;
    VkDeviceSize capacity = 0;
    VkDeviceSize used = 0;
};

struct FrameData {
    std::vector<std::shared_ptr<void>> retained;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence presentFence = VK_NULL_HANDLE;
    bool presentPending = false;
    VkQueryPool queries = VK_NULL_HANDLE;
    bool queriesWritten = false;
    uint64 serial = 0;
    VertexRing ring;
    VertexRing uniforms;
    VkDescriptorSet uniformSet = VK_NULL_HANDLE;
};

struct UploadBatch {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint64 serial = 0;
};

struct Swapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D extent{};
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool transferSource = false;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> renderFinished;
    VkImage depth = VK_NULL_HANDLE;
    VmaAllocation depthAllocation = nullptr;
    VkImageView depthView = VK_NULL_HANDLE;
};

struct Capture {
    bool requested = false;
    bool recorded = false;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkDeviceSize size = 0;
    uint64 serial = 0;
    uint32 width = 0;
    uint32 height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

enum class SwapchainResult { Ready, Minimized, Failed };

struct InstanceValidation {
    std::atomic<uint32> errors = 0;
    std::atomic<uint32> warnings = 0;
};
struct InstanceLease;

struct DeviceState {
    DeviceState() = default;
    DeviceState(const DeviceState&) = delete;
    DeviceState& operator=(const DeviceState&) = delete;

    Window* window = nullptr;
    GpuDeviceConfig config;
    GpuDeviceInfo info;
    GpuStatus status = GpuStatus::Uninitialized;
    GpuStatus lastFailure = GpuStatus::Ready;
    std::string lastError;

    std::shared_ptr<InstanceLease> instanceLease;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VmaAllocator allocator = nullptr;
    uint32 apiVersion = VK_API_VERSION_1_1;
    bool portabilityEnumeration = false;
    bool portabilitySubset = false;
    bool surfaceMaintenance1 = false;
    float timestampPeriod = 0.0f;
    Ref<InstanceValidation> instanceValidation = CreateRef<InstanceValidation>();
    uint32 validationErrorsAtInitialization = 0;
    uint32 validationWarningsAtInitialization = 0;

    Swapchain swapchain;
    bool swapchainDirty = false;
    uint32 swapchainFailures = 0;
    uint32 frameFailures = 0;
    bool vsync = true;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFormat renderPassFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<uint32> vertexCode;
    std::vector<uint32> fragmentCode;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    VkPipeline pipeline2D = VK_NULL_HANDLE;
    VkPipeline pipeline3D = VK_NULL_HANDLE;
    VkPipeline pipelineMesh = VK_NULL_HANDLE;
    VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool framePool = VK_NULL_HANDLE;
    VkDeviceSize uniformAlignment = 256;
    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler nearestSampler = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> descriptorPools;

    VkSemaphore timeline = VK_NULL_HANDLE;
    std::deque<std::pair<uint64, VkFence>> inflightFences;
    std::vector<VkFence> freeFences;
    uint64 submittedSerial = 0;
    uint64 completedSerial = 0;

    std::deque<Retired> retired;
    std::vector<Retired> pendingRetire;

    VkCommandPool uploadPool = VK_NULL_HANDLE;
    std::vector<UploadBatch> uploadBatches;
    VkCommandBuffer uploadRecording = VK_NULL_HANDLE;
    std::vector<Retired> uploadStaging;
    uint64 batchStagingBytes = 0;
    uint64 liveStagingBytes = 0;

    std::array<FrameData, MaxFramesInFlight> frames{};
    uint32 frameCount = 2;
    uint32 frameSlot = 0;
    uint32 imageIndex = 0;
    bool frameActive = false;
    bool renderPassActive = false;
    bool commandsOpen = false;
    VkClearColorValue clearColor{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    VkDescriptorSet boundSet = VK_NULL_HANDLE;
    VkBuffer boundVertexBuffer = VK_NULL_HANDLE;
    VkBuffer boundIndexBuffer = VK_NULL_HANDLE;
    uint32 lightOffset = 0;
    uint32 boundObjectOffset = UINT32_MAX;
    uint32 boundLightOffset = UINT32_MAX;
    Mat4 boundMatrix;
    bool matrixBound = false;
    Capture capture;

    HandlePool<BufferRecord> buffers;
    HandlePool<TextureRecord> textures;
    uint64 whiteTexture = 0;

    GpuFrameStats frameStats;
    GpuFrameStats lastFrameStats;
    GpuDeviceCounters counters;
    uint64 frameIndex = 0;

    bool Fail(VkResult result, std::string_view what);
    bool Fail(GpuStatus failure, std::string_view what);
    bool Usable() const {
        return status == GpuStatus::Ready;
    }

    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateDevice();
    void DestroyContext();

    SwapchainResult CreateSwapchain();
    void DestroySwapchain(Swapchain& target);
    VkFormat SurfaceFormat();
    bool RecreateSwapchain();

    bool LoadShaders();
    bool CreateRenderPass(VkFormat colorFormat);
    bool CreatePipelines();
    void DestroyPipelines();
    bool AllocateTextureSet(VkImageView view, VkDescriptorPool& pool, VkDescriptorSet& set,
                            TextureFilter filter);

    bool CreateSync();
    void DestroySync();
    uint64 PollCompleted();
    bool WaitForSerial(uint64 serial);
    bool WaitForPresent(FrameData& frame, uint64 timeout, bool reset = true);
    bool WaitForPresents(uint64 timeout, bool reset = true);
    VkResult Submit(VkCommandBuffer cmd, VkSemaphore wait, VkPipelineStageFlags waitStage,
                    VkSemaphore signal, uint64& serial);
    void Retire(const Retired& resource);
    void StampPending(uint64 serial);
    void CollectGarbage();
    void DestroyRetired(const Retired& resource);

    VkCommandBuffer UploadCommands();
    bool FlushUploads();
    bool CreateStaging(const void* data, VkDeviceSize size, Retired& staging, VkBuffer& buffer);
    bool CreateBufferResource(VkDeviceSize size, VkBufferUsageFlags usage, BufferRecord& record);
    bool UploadBuffer(BufferRecord& record, const void* data, VkDeviceSize size,
                      VkDeviceSize offset);
    bool CreateTextureResource(uint32 width, uint32 height, const void* pixels,
                               TextureRecord& record, TextureFilter filter = TextureFilter::Linear,
                               TextureColorSpace colorSpace = TextureColorSpace::Linear);
    bool EnsureVertexCapacity(FrameData& frame, VkDeviceSize bytes);
    bool EnsureCaptureBuffer(VkDeviceSize size);
    bool PushUniform(const void* data, VkDeviceSize size, uint32& offset);
    void BindFrameSet(uint32 objectOffset);
    bool ResetFrameUniforms();

    bool CreateFrames();
    void DestroyFrames();
    bool BeginRenderPass();
    void Shutdown();
};

} // namespace LuxEngine::Vk
