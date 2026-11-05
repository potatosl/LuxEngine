#include "Engine/Renderer/Ogre/OgreBridge.h"

#include <OgreCamera.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreManualObject.h>
#include <OgreMaterialManager.h>
#include <OgrePass.h>
#include <OgreRenderQueue.h>
#include <OgreRenderWindow.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreTechnique.h>
#include <OgreTextureManager.h>
#include <OgreTextureUnitState.h>
#include <OgreViewport.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::atomic<bool> activeRoot{false};
}

struct LuxOgreContext;

struct LuxOgreTexture {
    LuxOgreContext* owner = nullptr;
    Ogre::TexturePtr image;
    Ogre::MaterialPtr transparentMaterial;
    Ogre::MaterialPtr depthMaterial;
    bool pendingDestroy = false;
};

struct LuxOgreContext {
    struct Group {
        Ogre::SceneManager* scene = nullptr;
        Ogre::Camera* camera = nullptr;
        Ogre::Viewport* viewport = nullptr;
        std::vector<Ogre::ManualObject*> objects;
        float matrix[16] = {};
        bool depth = false;
    };

    std::unique_ptr<Ogre::Root> root;
    Ogre::RenderWindow* window = nullptr;
    SDL_Window* sdlWindow = nullptr;
    Ogre::MaterialPtr solidTransparent;
    Ogre::MaterialPtr solidDepth;
    std::vector<Group> groups;
    std::vector<LuxOgreTexture*> textures;
    size_t activeGroups = 0;
    unsigned nextResourceId = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    Ogre::ColourValue clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    bool frameOpen = false;
    bool ownsRootSlot = false;

    ~LuxOgreContext();
    void ClearGroups();
    void ReleaseTexture(LuxOgreTexture* texture);
    void SweepTextures();
};

namespace {
struct RootSlotReservation {
    bool committed = false;
    ~RootSlotReservation() {
        if (!committed)
            activeRoot.store(false, std::memory_order_release);
    }
};

void WriteError(char* destination, size_t capacity, const char* message) {
    if (!destination || capacity == 0)
        return;
    std::strncpy(destination, message, capacity - 1);
    destination[capacity - 1] = '\0';
}

std::string ResourceName(LuxOgreContext& context, const char* kind) {
    return std::string("LuxEngine_") + kind + "_" +
           std::to_string(reinterpret_cast<std::uintptr_t>(&context)) + "_" +
           std::to_string(context.nextResourceId++);
}

std::string OgreLogPath() {
    char* directory = SDL_GetPrefPath("LuxEngine", "LuxEngine");
    if (!directory)
        return "Ogre.log";
    std::string result = std::string(directory) + "Ogre.log";
    SDL_free(directory);
    return result;
}

void RemoveMaterial(Ogre::MaterialPtr& material) {
    if (material.isNull())
        return;
    const Ogre::String name = material->getName();
    material.setNull();
    Ogre::MaterialManager::getSingleton().remove(name);
}

Ogre::MaterialPtr MakeMaterial(LuxOgreContext& context, bool depth,
                               const Ogre::TexturePtr& texture = Ogre::TexturePtr()) {
    Ogre::MaterialPtr material = Ogre::MaterialManager::getSingleton().create(
        ResourceName(context, "Material"), Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
    try {
        Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
        pass->setLightingEnabled(false);
        pass->setCullingMode(Ogre::CULL_NONE);
        pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
        pass->setDepthCheckEnabled(depth);
        pass->setDepthWriteEnabled(depth);
        pass->setSceneBlending(depth ? Ogre::SBT_REPLACE : Ogre::SBT_TRANSPARENT_ALPHA);
        if (!texture.isNull()) {
            Ogre::TextureUnitState* unit = pass->createTextureUnitState(texture->getName());
            unit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);
            unit->setTextureFiltering(Ogre::TFO_BILINEAR);
        }
        material->load();
        return material;
    } catch (...) {
        RemoveMaterial(material);
        throw;
    }
}

void SetProjection(Ogre::Camera& camera, const float* columnMajor) {
    Ogre::Matrix4 projection(columnMajor[0], columnMajor[4], columnMajor[8], columnMajor[12],
                             columnMajor[1], columnMajor[5], columnMajor[9], columnMajor[13],
                             columnMajor[2], columnMajor[6], columnMajor[10], columnMajor[14],
                             columnMajor[3], columnMajor[7], columnMajor[11], columnMajor[15]);
    // LuxEngine matrices use [0, 1] clip depth; Ogre custom projections use [-1, 1].
    for (size_t column = 0; column < 4; ++column)
        projection[2][column] = 2.0f * projection[2][column] - projection[3][column];
    camera.setCustomViewMatrix(true, Ogre::Matrix4::IDENTITY);
    camera.setCustomProjectionMatrix(true, projection);
}

LuxOgreContext::Group& NextGroup(LuxOgreContext& context, const float* matrix, bool depth) {
    if (context.activeGroups != 0) {
        auto& last = context.groups[context.activeGroups - 1];
        if (last.depth == depth && std::memcmp(last.matrix, matrix, sizeof(last.matrix)) == 0)
            return last;
    }
    const size_t index = context.activeGroups;
    if (index == context.groups.size()) {
        LuxOgreContext::Group group;
        group.scene =
            context.root->createSceneManager(Ogre::ST_GENERIC, ResourceName(context, "Scene"));
        try {
            group.camera = group.scene->createCamera(ResourceName(context, "Camera"));
            group.camera->setNearClipDistance(0.01f);
            group.camera->setFarClipDistance(100'000.0f);
            context.groups.push_back(group);
        } catch (...) {
            context.root->destroySceneManager(group.scene);
            throw;
        }
    }
    auto& group = context.groups[index];
    std::memcpy(group.matrix, matrix, sizeof(group.matrix));
    group.depth = depth;
    try {
        SetProjection(*group.camera, matrix);
        group.viewport = context.window->addViewport(group.camera, static_cast<int>(index));
        group.viewport->setOverlaysEnabled(false);
        group.viewport->setShadowsEnabled(false);
        group.viewport->setBackgroundColour(context.clearColor);
        // Preserve colour and depth across draw groups in submission order.
        group.viewport->setClearEveryFrame(index == 0, Ogre::FBT_COLOUR | Ogre::FBT_DEPTH);
    } catch (...) {
        if (context.window->hasViewportWithZOrder(static_cast<int>(index)))
            context.window->removeViewport(static_cast<int>(index));
        group.viewport = nullptr;
        throw;
    }
    ++context.activeGroups;
    return group;
}

bool SyncWindowSize(LuxOgreContext& context) {
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(context.sdlWindow, &width, &height) || width <= 0 || height <= 0)
        return false;
    if (width != context.pixelWidth || height != context.pixelHeight) {
        context.window->resize(static_cast<unsigned>(width), static_cast<unsigned>(height));
        context.window->windowMovedOrResized();
        context.pixelWidth = width;
        context.pixelHeight = height;
    }
    return true;
}
} // namespace

void LuxOgreContext::ReleaseTexture(LuxOgreTexture* texture) {
    if (!texture)
        return;
    try {
        RemoveMaterial(texture->transparentMaterial);
    } catch (...) {
    }
    try {
        RemoveMaterial(texture->depthMaterial);
    } catch (...) {
    }
    try {
        if (!texture->image.isNull()) {
            const Ogre::String name = texture->image->getName();
            texture->image.setNull();
            Ogre::TextureManager::getSingleton().remove(name);
        }
    } catch (...) {
    }
    delete texture;
}

void LuxOgreContext::SweepTextures() {
    for (auto it = textures.begin(); it != textures.end();) {
        if ((*it)->pendingDestroy) {
            ReleaseTexture(*it);
            it = textures.erase(it);
        } else {
            ++it;
        }
    }
}

void LuxOgreContext::ClearGroups() {
    if (!root)
        return;
    if (window)
        window->removeAllViewports();
    for (auto& group : groups) {
        group.viewport = nullptr;
        if (!group.scene)
            continue;
        for (Ogre::ManualObject* object : group.objects)
            group.scene->destroyManualObject(object);
        group.objects.clear();
    }
    activeGroups = 0;
}

LuxOgreContext::~LuxOgreContext() {
    try {
        if (root) {
            ClearGroups();
            for (auto* texture : textures)
                ReleaseTexture(texture);
            textures.clear();
            RemoveMaterial(solidTransparent);
            RemoveMaterial(solidDepth);
            for (auto& group : groups) {
                if (group.scene)
                    root->destroySceneManager(group.scene);
            }
            groups.clear();
        }
    } catch (...) {
        // Destructors must not throw across the C ABI.
    }
    root.reset();
    if (ownsRootSlot)
        activeRoot.store(false, std::memory_order_release);
}

extern "C" LuxOgreContext* lux_ogre_create(void* sdl_window, const char* plugin_path,
                                           int initial_vsync, char* error, size_t error_capacity) {
    if (!sdl_window || !plugin_path || !plugin_path[0]) {
        WriteError(error, error_capacity, "SDL window or Ogre GL plugin is missing");
        return nullptr;
    }
    bool expected = false;
    if (!activeRoot.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        WriteError(error, error_capacity, "Ogre renderer is already initialized");
        return nullptr;
    }
    RootSlotReservation reservation;
    try {
        auto context = std::unique_ptr<LuxOgreContext>(new LuxOgreContext);
        context->sdlWindow = static_cast<SDL_Window*>(sdl_window);
        const SDL_PropertiesID properties = SDL_GetWindowProperties(context->sdlWindow);
        Ogre::NameValuePairList parameters;
        parameters["vsync"] = initial_vsync ? "true" : "false";
#if defined(__linux__)
        // Ogre 1.9's GL plugin only supports an X11/GLX external window here.
        if (!SDL_GetCurrentVideoDriver() || std::strcmp(SDL_GetCurrentVideoDriver(), "x11") != 0)
            throw std::runtime_error("Ogre 1.9 requires an SDL X11 window on Linux");
        void* display =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        const Sint64 screen =
            SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_SCREEN_NUMBER, -1);
        const Sint64 window =
            SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        if (!display || screen < 0 || window <= 0)
            throw std::runtime_error("SDL X11 window handle is unavailable");
        parameters["externalWindowHandle"] =
            std::to_string(reinterpret_cast<std::uintptr_t>(display)) + ":" +
            std::to_string(screen) + ":" + std::to_string(window);
#elif defined(_WIN32)
        void* nativeWindow =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (!nativeWindow)
            throw std::runtime_error("SDL Win32 window handle is unavailable");
        parameters["externalWindowHandle"] =
            std::to_string(reinterpret_cast<std::uintptr_t>(nativeWindow));
#elif defined(__APPLE__)
        void* nativeWindow =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        if (!nativeWindow)
            throw std::runtime_error("SDL Cocoa window handle is unavailable");
        parameters["externalWindowHandle"] =
            std::to_string(reinterpret_cast<std::uintptr_t>(nativeWindow));
        parameters["macAPI"] = "cocoa";
#else
        throw std::runtime_error("Unsupported Ogre window platform");
#endif
        if (!SDL_GetWindowSizeInPixels(context->sdlWindow, &context->pixelWidth,
                                       &context->pixelHeight) ||
            context->pixelWidth <= 0 || context->pixelHeight <= 0)
            throw std::runtime_error("SDL window pixel size is unavailable");

        // Validate native window access before loading GL: the old plugin can abort
        // while probing a missing X display instead of throwing an Ogre exception.
        if (Ogre::Root::getSingletonPtr())
            throw std::runtime_error("An Ogre Root is already active in this process");
        context->root.reset(new Ogre::Root("", "", OgreLogPath()));
        context->root->loadPlugin(plugin_path);
        const Ogre::RenderSystemList& systems = context->root->getAvailableRenderers();
        if (systems.empty())
            throw std::runtime_error("Ogre GL render system is unavailable");
        context->root->setRenderSystem(systems.front());
        context->root->initialise(false);
        context->window = context->root->createRenderWindow(
            "LuxEngine", static_cast<unsigned>(context->pixelWidth),
            static_cast<unsigned>(context->pixelHeight), false, &parameters);
        context->window->setActive(true);
        context->solidTransparent = MakeMaterial(*context, false);
        context->solidDepth = MakeMaterial(*context, true);
        context->ownsRootSlot = true;
        reservation.committed = true;
        return context.release();
    } catch (const Ogre::Exception& exception) {
        WriteError(error, error_capacity, exception.getFullDescription().c_str());
    } catch (const std::exception& exception) {
        WriteError(error, error_capacity, exception.what());
    } catch (...) {
        WriteError(error, error_capacity, "Unknown Ogre initialization error");
    }
    return nullptr;
}

extern "C" void lux_ogre_destroy(LuxOgreContext* context) {
    delete context;
}

extern "C" int lux_ogre_begin_frame(LuxOgreContext* context) {
    if (!context || context->frameOpen)
        return 0;
    try {
        context->ClearGroups();
        context->SweepTextures();
        if (!SyncWindowSize(*context))
            return 0;
        context->frameOpen = true;
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int lux_ogre_clear(LuxOgreContext* context, float r, float g, float b, float a) {
    if (!context || !context->frameOpen)
        return 0;
    context->clearColor = Ogre::ColourValue(r, g, b, a);
    for (size_t i = 0; i < context->activeGroups; ++i)
        context->groups[i].viewport->setBackgroundColour(context->clearColor);
    return 1;
}

extern "C" int lux_ogre_draw_triangles(LuxOgreContext* context, const LuxOgreVertex* vertices,
                                       size_t count, const float* view_projection_column_major,
                                       LuxOgreTexture* texture, int depth) {
    if (!context || !context->frameOpen || !vertices || !view_projection_column_major || !count ||
        count % 3 != 0 || (texture && (texture->owner != context || texture->pendingDestroy)))
        return 0;
    LuxOgreContext::Group* group = nullptr;
    Ogre::ManualObject* object = nullptr;
    try {
        group = &NextGroup(*context, view_projection_column_major, depth != 0);
        Ogre::MaterialPtr& material =
            texture ? (depth ? texture->depthMaterial : texture->transparentMaterial)
                    : (depth ? context->solidDepth : context->solidTransparent);
        object = group->scene->createManualObject();
        object->estimateVertexCount(count);
        object->begin(material->getName(), Ogre::RenderOperation::OT_TRIANGLE_LIST);
        for (size_t i = 0; i < count; ++i) {
            const LuxOgreVertex& vertex = vertices[i];
            object->position(vertex.x, vertex.y, vertex.z);
            object->colour(vertex.r, vertex.g, vertex.b, vertex.a);
            object->textureCoord(vertex.u, vertex.v);
        }
        object->end();
        object->setRenderQueueGroupAndPriority(
            depth ? Ogre::RENDER_QUEUE_MAIN
                  : static_cast<Ogre::uint8>(Ogre::RENDER_QUEUE_OVERLAY - 1),
            static_cast<Ogre::ushort>(std::min<size_t>(group->objects.size(), 65'535)));
        group->scene->getRootSceneNode()->attachObject(object);
        group->objects.push_back(object);
        return 1;
    } catch (...) {
        if (group && object) {
            try {
                group->scene->destroyManualObject(object);
            } catch (...) {
            }
        }
        return 0;
    }
}

extern "C" int lux_ogre_end_frame(LuxOgreContext* context) {
    if (!context || !context->frameOpen)
        return 0;
    context->frameOpen = false;
    try {
        if (context->activeGroups == 0) {
            const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            NextGroup(*context, identity, false);
        }
        return context->root->renderOneFrame() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" int lux_ogre_set_vsync(LuxOgreContext* context, int enabled) {
    if (!context || !context->window || context->frameOpen)
        return 0;
    try {
        context->window->setVSyncEnabled(enabled != 0);
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" LuxOgreTexture* lux_ogre_texture_create(LuxOgreContext* context, unsigned width,
                                                   unsigned height, const void* rgba_pixels) {
    if (!context || !width || !height || !rgba_pixels ||
        static_cast<std::uint64_t>(width) * height > std::numeric_limits<unsigned>::max() / 4u)
        return nullptr;
    std::unique_ptr<LuxOgreTexture> texture;
    try {
        texture.reset(new LuxOgreTexture);
        texture->owner = context;
        texture->image = Ogre::TextureManager::getSingleton().createManual(
            ResourceName(*context, "Texture"),
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, Ogre::TEX_TYPE_2D, width,
            height, 0, Ogre::PF_BYTE_RGBA, Ogre::TU_STATIC_WRITE_ONLY);
        Ogre::PixelBox source(width, height, 1, Ogre::PF_BYTE_RGBA, const_cast<void*>(rgba_pixels));
        texture->image->getBuffer()->blitFromMemory(source);
        texture->transparentMaterial = MakeMaterial(*context, false, texture->image);
        texture->depthMaterial = MakeMaterial(*context, true, texture->image);
        context->textures.push_back(texture.get());
        return texture.release();
    } catch (...) {
        context->ReleaseTexture(texture.release());
        return nullptr;
    }
}

extern "C" void lux_ogre_texture_destroy(LuxOgreContext* context, LuxOgreTexture* texture) {
    if (!context || !texture || texture->owner != context)
        return;
    texture->pendingDestroy = true;
}
