#pragma once
#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/ECS/ECS.hpp>

#if defined(_WIN32)
#define LUX_SCRIPT_CALL __cdecl
#else
#define LUX_SCRIPT_CALL
#endif

namespace LuxEngine::Managed {
struct NativeApi {
    uint32 version;
    uint32 size;
    int(LUX_SCRIPT_CALL* isAlive)(uint64 world, uint32 entity);
    int(LUX_SCRIPT_CALL* getTransform)(uint64 world, uint32 entity, TransformComponent* value);
    int(LUX_SCRIPT_CALL* setTransform)(uint64 world, uint32 entity,
                                       const TransformComponent* value);
    int(LUX_SCRIPT_CALL* isKeyPressed)(int key);
};

struct ManagedApi {
    uint32 version;
    uint32 size;
    uint64(LUX_SCRIPT_CALL* load)(uint64 world, const char* path, int length);
    int(LUX_SCRIPT_CALL* attach)(uint64 context, uint32 entity, const char* type, int length,
                                 const char* fields, int fieldLength);
    int(LUX_SCRIPT_CALL* detach)(uint64 context, uint32 entity);
    int(LUX_SCRIPT_CALL* update)(uint64 context, double deltaTime);
    void(LUX_SCRIPT_CALL* unload)(uint64 context);
    int(LUX_SCRIPT_CALL* describe)(uint64 context, const char* type, int length, char* destination,
                                   int capacity);
    int(LUX_SCRIPT_CALL* lastError)(char* destination, int capacity);
};

using GetApi = int(LUX_SCRIPT_CALL*)(const NativeApi*, ManagedApi*);
} // namespace LuxEngine::Managed
