#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
#define LUX_PLATFORM_WINDOWS 1
#elif defined(__linux__)
#define LUX_PLATFORM_LINUX 1
#elif defined(__APPLE__)
#define LUX_PLATFORM_MACOS 1
#endif

#if defined(_WIN32) && defined(LUX_ENGINE_SHARED)
#if defined(LUX_ENGINE_BUILD)
#define LUX_API __declspec(dllexport)
#else
#define LUX_API __declspec(dllimport)
#endif
#else
#define LUX_API
#endif

namespace LuxEngine {

using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

template <typename T> using Scope = std::unique_ptr<T>;

template <typename T, typename... Args> inline Scope<T> CreateScope(Args&&... args);

template <typename T> using Ref = std::shared_ptr<T>;

template <typename T, typename... Args> inline Ref<T> CreateRef(Args&&... args);

} // namespace LuxEngine

#include <LuxEngine/Core/Core.inl>
