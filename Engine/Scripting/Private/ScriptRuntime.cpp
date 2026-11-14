#include "ScriptAbi.hpp"
#include <LuxEngine/Platform/Input.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxEngine/Scripting/ScriptRuntime.hpp>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_loadso.h>
#include <atomic>
#include <cmath>
#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <map>
#include <mutex>
#include <nethost.h>
#include <thread>
#include <type_traits>
#include <unordered_map>

namespace LuxEngine {
namespace {
static_assert(sizeof(TransformComponent) == 9 * sizeof(float));
static_assert(std::is_standard_layout_v<TransformComponent>);

struct WorldState {
    Scene* scene = nullptr;
    std::weak_ptr<int> lifetime;
    std::thread::id owner;
};

std::mutex registryMutex;
std::unordered_map<uint64, std::weak_ptr<WorldState>> worlds;
std::atomic<uint64> nextWorld = 0;
std::recursive_mutex managedMutex;

std::shared_ptr<WorldState> FindWorld(uint64 id) {
    std::lock_guard lock(registryMutex);
    const auto found = worlds.find(id);
    if (found == worlds.end())
        return {};
    auto world = found->second.lock();
    if (!world || world->owner != std::this_thread::get_id() || world->lifetime.expired())
        return {};
    return world;
}

int LUX_SCRIPT_CALL IsAlive(uint64 id, uint32 entity) {
    const auto world = FindWorld(id);
    return world && world->scene->IsAlive(entity) ? 1 : 0;
}

int LUX_SCRIPT_CALL GetTransform(uint64 id, uint32 entity, TransformComponent* output) {
    const auto world = FindWorld(id);
    if (!world || !output)
        return 0;
    const auto* transform = world->scene->TryGetComponent<TransformComponent>(entity);
    if (!transform)
        return 0;
    *output = *transform;
    return 1;
}

int LUX_SCRIPT_CALL SetTransform(uint64 id, uint32 entity, const TransformComponent* value) {
    const auto world = FindWorld(id);
    if (!world || !value)
        return 0;
    const auto finite = [](const Vec3& vector) {
        return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
    };
    if (!finite(value->position) || !finite(value->rotation) || !finite(value->scale))
        return 0;
    auto* transform = world->scene->TryGetComponent<TransformComponent>(entity);
    if (!transform)
        return 0;
    *transform = *value;
    return 1;
}

int LUX_SCRIPT_CALL IsKeyPressed(int key) {
    return Input::IsKeyPressed(key) ? 1 : 0;
}

std::basic_string<char_t> HostPath(const std::filesystem::path& path) {
#if defined(_WIN32)
    return path.wstring();
#else
    return path.string();
#endif
}

std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path ExecutableDirectory() {
    const char* path = SDL_GetBasePath();
    return path ? std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path)))
                : std::filesystem::path{};
}

struct DotnetHost {
    SDL_SharedObject* discovery = nullptr;
    SDL_SharedObject* runtime = nullptr;
    Managed::ManagedApi api = {};
    std::filesystem::path core;

    // Runtime modules remain loaded until process termination. CoreCLR cannot be unloaded safely.
    bool Initialize(const ScriptRuntimeConfig& config, std::string& error) {
        const auto directory =
            config.coreDirectory.empty() ? ExecutableDirectory() / "managed" : config.coreDirectory;
        const auto assembly =
            std::filesystem::weakly_canonical(directory / "LuxEngine.ScriptCore.dll");
        if (api.version) {
            if (assembly != core) {
                error = "다른 버전의 C# 스크립트 코어가 이미 로드되어 있습니다.";
                return false;
            }
            return true;
        }
        const auto hostDirectory =
            config.hostDirectory.empty() ? ExecutableDirectory() : config.hostDirectory;
#if defined(_WIN32)
        constexpr const char* discoveryName = "nethost.dll";
#elif defined(__APPLE__)
        constexpr const char* discoveryName = "libnethost.dylib";
#else
        constexpr const char* discoveryName = "libnethost.so";
#endif
        if (!discovery)
            discovery = SDL_LoadObject(Utf8(hostDirectory / discoveryName).c_str());
        if (!discovery) {
            error = "C# 호스팅 라이브러리를 읽을 수 없습니다: " + std::string(SDL_GetError());
            return false;
        }
        const auto discover = reinterpret_cast<decltype(&get_hostfxr_path)>(
            SDL_LoadFunction(discovery, "get_hostfxr_path"));
        if (!discover) {
            error = "C# 호스팅 API 버전이 올바르지 않습니다.";
            return false;
        }
        std::basic_string<char_t> dotnetRoot;
        get_hostfxr_parameters parameters = {sizeof(get_hostfxr_parameters), nullptr, nullptr};
        if (!config.dotnetRoot.empty()) {
            dotnetRoot = HostPath(config.dotnetRoot);
            parameters.dotnet_root = dotnetRoot.c_str();
        }
        size_t length = 0;
        discover(nullptr, &length, &parameters);
        if (length == 0 || length > 32768) {
            error = ".NET 10 런타임을 찾을 수 없습니다.";
            return false;
        }
        std::vector<char_t> path(length);
        if (discover(path.data(), &length, &parameters) != 0) {
            error = ".NET 런타임 경로를 확인할 수 없습니다.";
            return false;
        }
        if (!runtime)
            runtime = SDL_LoadObject(Utf8(std::filesystem::path(path.data())).c_str());
        if (!runtime) {
            error = ".NET 런타임을 로드할 수 없습니다: " + std::string(SDL_GetError());
            return false;
        }
        const auto initialize = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
            SDL_LoadFunction(runtime, "hostfxr_initialize_for_runtime_config"));
        const auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
            SDL_LoadFunction(runtime, "hostfxr_get_runtime_delegate"));
        const auto close =
            reinterpret_cast<hostfxr_close_fn>(SDL_LoadFunction(runtime, "hostfxr_close"));
        if (!initialize || !getDelegate || !close) {
            error = ".NET 호스팅 API를 확인할 수 없습니다.";
            return false;
        }
        const auto runtimeConfig = HostPath(directory / "LuxEngine.ScriptCore.runtimeconfig.json");
        hostfxr_handle context = nullptr;
        const int status = initialize(runtimeConfig.c_str(), nullptr, &context);
        if (status < 0 || !context) {
            error = "C# 스크립트 런타임을 시작하지 못했습니다. .NET 10 설치를 확인해 주세요.";
            return false;
        }
        load_assembly_and_get_function_pointer_fn load = nullptr;
        const int result = getDelegate(context, hdt_load_assembly_and_get_function_pointer,
                                       reinterpret_cast<void**>(&load));
        close(context);
        if (result < 0 || !load) {
            error = "C# 어셈블리 로더를 초기화하지 못했습니다.";
            return false;
        }
        const auto nativeAssembly = HostPath(assembly);
#if defined(_WIN32)
        constexpr const wchar_t* type = L"LuxEngine.NativeBridge, LuxEngine.ScriptCore";
        constexpr const wchar_t* method = L"GetApi";
#else
        constexpr const char* type = "LuxEngine.NativeBridge, LuxEngine.ScriptCore";
        constexpr const char* method = "GetApi";
#endif
        Managed::GetApi getApi = nullptr;
        if (load(nativeAssembly.c_str(), type, method, UNMANAGEDCALLERSONLY_METHOD, nullptr,
                 reinterpret_cast<void**>(&getApi)) < 0 ||
            !getApi) {
            error = "C# 스크립트 코어 진입점을 찾을 수 없습니다.";
            return false;
        }
        const Managed::NativeApi native = {
            2, sizeof(Managed::NativeApi), IsAlive, GetTransform, SetTransform, IsKeyPressed};
        if (!getApi(&native, &api) || api.version != 2 || api.size != sizeof(api)) {
            api = {};
            error = "C# 스크립트 ABI가 엔진과 일치하지 않습니다.";
            return false;
        }
        core = assembly;
        return true;
    }
};

DotnetHost& Host() {
    static DotnetHost host;
    return host;
}

std::string ManagedError() {
    std::vector<char> message(16384);
    Host().api.lastError(message.data(), static_cast<int>(message.size()));
    return message.data();
}
} // namespace

struct ScriptRuntime::Impl {
    std::shared_ptr<WorldState> world;
    uint64 nativeId = 0;
    uint64 context = 0;
    std::map<uint32, std::pair<std::string, std::string>> instances;
    std::string error;
};

ScriptRuntime::ScriptRuntime() : impl_(std::make_unique<Impl>()) {}

ScriptRuntime::~ScriptRuntime() {
    Shutdown();
}

bool ScriptRuntime::Initialize(Scene& scene, const ScriptRuntimeConfig& config) {
    Shutdown();
    impl_->error.clear();
    try {
        std::lock_guard managedLock(managedMutex);
        if (!Host().Initialize(config, impl_->error))
            return false;
        const auto assembly = Utf8(std::filesystem::canonical(config.assemblyPath));
        impl_->world = std::make_shared<WorldState>();
        impl_->world->scene = &scene;
        impl_->world->lifetime = scene.Lifetime();
        impl_->world->owner = std::this_thread::get_id();
        impl_->nativeId = ++nextWorld;
        {
            std::lock_guard registryLock(registryMutex);
            worlds[impl_->nativeId] = impl_->world;
        }
        impl_->context =
            Host().api.load(impl_->nativeId, assembly.data(), static_cast<int>(assembly.size()));
        if (!impl_->context) {
            impl_->error = ManagedError();
            Shutdown();
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        Shutdown();
        return false;
    }
}

bool ScriptRuntime::Update(double deltaTime) {
    if (!IsRunning() || !std::isfinite(deltaTime) || deltaTime < 0)
        return false;
    if (impl_->world->owner != std::this_thread::get_id()) {
        impl_->error = "C# 스크립트는 씬을 소유한 스레드에서 실행해야 합니다.";
        return false;
    }
    if (impl_->world->lifetime.expired()) {
        impl_->error = "스크립트가 연결된 씬이 교체되거나 종료되었습니다.";
        Shutdown();
        return false;
    }
    std::lock_guard lock(managedMutex);
    auto& scene = *impl_->world->scene;
    for (auto iterator = impl_->instances.begin(); iterator != impl_->instances.end();) {
        const auto* component = scene.TryGetComponent<ScriptComponent>(iterator->first);
        std::string fields;
        if (component && !SerializeScriptFields(component->fields, fields)) {
            impl_->error = "Invalid serialized C# fields";
            return false;
        }
        if (!component || !component->enabled || component->typeName != iterator->second.first ||
            fields != iterator->second.second) {
            if (!Host().api.detach(impl_->context, iterator->first)) {
                impl_->error = ManagedError();
                return false;
            }
            iterator = impl_->instances.erase(iterator);
        } else {
            ++iterator;
        }
    }
    for (const uint32 entity : scene.GetComponentEntityIds<ScriptComponent>()) {
        const auto& component = scene.GetComponent<ScriptComponent>(entity);
        if (!component.enabled || impl_->instances.contains(entity))
            continue;
        if (component.typeName.empty()) {
            impl_->error = "C# 스크립트 클래스 이름이 비어 있습니다.";
            return false;
        }
        std::string fields;
        if (!SerializeScriptFields(component.fields, fields)) {
            impl_->error = "Invalid serialized C# fields";
            return false;
        }
        if (!Host().api.attach(impl_->context, entity, component.typeName.data(),
                               static_cast<int>(component.typeName.size()), fields.data(),
                               static_cast<int>(fields.size()))) {
            impl_->error = ManagedError();
            return false;
        }
        impl_->instances.emplace(entity, std::make_pair(component.typeName, fields));
    }
    if (!Host().api.update(impl_->context, deltaTime)) {
        impl_->error = ManagedError();
        return false;
    }
    return true;
}

bool ScriptRuntime::DescribeFields(const std::string& typeName,
                                   std::vector<ScriptFieldDescription>& fields) {
    if (!IsRunning() || impl_->world->owner != std::this_thread::get_id() ||
        impl_->world->lifetime.expired())
        return false;
    std::lock_guard lock(managedMutex);
    std::vector<char> buffer(65537);
    const int size =
        Host().api.describe(impl_->context, typeName.data(), static_cast<int>(typeName.size()),
                            buffer.data(), static_cast<int>(buffer.size()));
    if (size < 0) {
        impl_->error = ManagedError();
        return false;
    }
    if (size > 65536) {
        impl_->error = "C# field metadata exceeds the 65536-byte UTF-8 limit";
        return false;
    }
    ScriptFieldMap defaults;
    if (!DeserializeScriptFields(std::string(buffer.data(), static_cast<size_t>(size)), defaults)) {
        impl_->error = "Invalid C# field metadata";
        return false;
    }
    std::vector<ScriptFieldDescription> next;
    for (auto& [name, value] : defaults)
        next.push_back({name, std::move(value)});
    fields = std::move(next);
    return true;
}

void ScriptRuntime::Shutdown() {
    if (!impl_)
        return;
    std::lock_guard managedLock(managedMutex);
    if (impl_->context) {
        Host().api.unload(impl_->context);
        impl_->context = 0;
    }
    {
        std::lock_guard registryLock(registryMutex);
        worlds.erase(impl_->nativeId);
    }
    impl_->instances.clear();
    impl_->world.reset();
    impl_->nativeId = 0;
}

bool ScriptRuntime::IsRunning() const {
    return impl_ && impl_->context != 0;
}

const std::string& ScriptRuntime::GetLastError() const {
    return impl_->error;
}
} // namespace LuxEngine
