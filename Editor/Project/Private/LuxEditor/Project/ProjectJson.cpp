#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Project/ProjectJson.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>

namespace LuxEditor {
namespace {
using Json = nlohmann::ordered_json;

void RequireValidProject(bool condition) {
    if (!condition)
        throw std::runtime_error("Invalid project JSON");
}

std::string ReadText(const Json& value) {
    RequireValidProject(value.is_string());
    return value.get<std::string>();
}

unsigned ReadUnsigned(const Json& value) {
    RequireValidProject(value.is_number_integer());
    if (value.is_number_unsigned()) {
        const auto number = value.get<uint64_t>();
        RequireValidProject(number <= std::numeric_limits<unsigned>::max());
        return static_cast<unsigned>(number);
    }
    const auto number = value.get<int64_t>();
    RequireValidProject(number >= 0 &&
                        static_cast<uint64_t>(number) <= std::numeric_limits<unsigned>::max());
    return static_cast<unsigned>(number);
}

bool ReadBoolean(const Json& value) {
    RequireValidProject(value.is_boolean());
    return value.get<bool>();
}
} // namespace

std::string SerializeProjectJson(const EditorProject& project, const std::filesystem::path& scene) {
    try {
        const auto& settings = project.GetSettings();
        Json dependencies = Json::array();
        for (const auto& dependency : settings.dependencies)
            dependencies.push_back(
                {{"package", dependency.package}, {"version", dependency.version}});
        Json root = {{"format", "LuxEngine.Project"},
                     {"version", 1},
                     {"name", project.GetName()},
                     {"engine_version", project.GetEngineVersion()},
                     {"project_version", settings.version},
                     {"template", ProjectTemplateName(project.GetTemplate())},
                     {"start_scene", GenericPathUtf8(scene)},
                     {"settings",
                      {{"icon", GenericPathUtf8(settings.icon)},
                       {"autosave_seconds", settings.autosaveSeconds},
                       {"backup_limit", settings.backupLimit},
                       {"snap_to_grid", settings.snapToGrid}}},
                     {"dependencies", std::move(dependencies)}};
        auto result = root.dump(2) + '\n';
        return result.size() <= 65536 ? result : std::string{};
    } catch (const std::exception&) {
        return {};
    }
}

bool DeserializeProjectJson(std::string_view data, ProjectSettings& settings,
                            std::string& engineVersion, std::string& templateName,
                            std::string& scene) {
    try {
        RequireValidProject(!data.empty() && data.size() <= 65536);
        std::vector<std::set<std::string>> keys;
        const auto root =
            Json::parse(data, [&](int depth, Json::parse_event_t event, Json& parsed) {
                RequireValidProject(depth <= 16);
                if (event == Json::parse_event_t::object_start)
                    keys.emplace_back();
                else if (event == Json::parse_event_t::key)
                    RequireValidProject(!keys.empty() &&
                                        keys.back().insert(parsed.get<std::string>()).second);
                else if (event == Json::parse_event_t::object_end)
                    keys.pop_back();
                return true;
            });
        RequireValidProject(root.is_object() && root.at("format") == "LuxEngine.Project" &&
                            ReadUnsigned(root.at("version")) == 1);
        settings.name = ReadText(root.at("name"));
        engineVersion = ReadText(root.at("engine_version"));
        settings.version = ReadText(root.at("project_version"));
        templateName = ReadText(root.at("template"));
        scene = ReadText(root.at("start_scene"));
        const auto& options = root.at("settings");
        settings.icon = PathFromUtf8(ReadText(options.at("icon")));
        settings.autosaveSeconds = ReadUnsigned(options.at("autosave_seconds"));
        settings.backupLimit = ReadUnsigned(options.at("backup_limit"));
        settings.snapToGrid = ReadBoolean(options.at("snap_to_grid"));
        const auto& dependencies = root.at("dependencies");
        RequireValidProject(dependencies.is_array() && dependencies.size() <= 64);
        settings.dependencies.clear();
        for (const auto& dependency : dependencies)
            settings.dependencies.push_back(
                {ReadText(dependency.at("package")), ReadText(dependency.at("version"))});
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
} // namespace LuxEditor
