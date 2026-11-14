#pragma once

#include <LuxEngine/Core/Math.hpp>
#include <map>
#include <string>
#include <variant>

namespace LuxEngine {
struct ScriptEntityReference {
    uint32 id = 0;
    bool operator==(const ScriptEntityReference&) const = default;
};

using ScriptFieldValue =
    std::variant<bool, int32, float, std::string, Vec2, Vec3, Vec4, ScriptEntityReference>;
using ScriptFieldMap = std::map<std::string, ScriptFieldValue>;

struct ScriptFieldDescription {
    std::string name;
    ScriptFieldValue defaultValue;
};

LUX_API bool SerializeScriptFields(const ScriptFieldMap& fields, std::string& output);
LUX_API bool DeserializeScriptFields(const std::string& input, ScriptFieldMap& fields);
} // namespace LuxEngine
