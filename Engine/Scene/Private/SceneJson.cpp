#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <type_traits>

namespace LuxEngine {
namespace {
using Json = nlohmann::ordered_json;
constexpr size_t MaxSceneBytes = 64ull * 1024 * 1024;
constexpr size_t MaxEntities = 100000;

void RequireValidScene(bool valid) {
    if (!valid)
        throw std::runtime_error("Invalid scene JSON");
}

template <class T> Json ToJson(const T& value) {
    return value;
}
Json ToJson(const Vec2& value) {
    return Json::array({value.x, value.y});
}
Json ToJson(const Vec3& value) {
    return Json::array({value.x, value.y, value.z});
}
Json ToJson(const Vec4& value) {
    return Json::array({value.x, value.y, value.z, value.w});
}
Json ToJson(BodyType value) {
    return static_cast<int>(value);
}
Json ToJson(SceneReferenceMode value) {
    return static_cast<int>(value);
}
Json ToJson(Render3DMode value) {
    return static_cast<int>(value);
}
Json ToJson(const std::vector<Keyframe>& keys) {
    Json result = Json::array();
    for (const auto& key : keys)
        result.push_back({{"time", key.time},
                          {"position", ToJson(key.position)},
                          {"rotation", ToJson(key.rotation)},
                          {"scale", ToJson(key.scale)}});
    return result;
}

template <class T> void ReadJsonValue(const Json& json, T& output) {
    if constexpr (std::is_same_v<T, bool>)
        RequireValidScene(json.is_boolean());
    else if constexpr (std::is_integral_v<T>) {
        RequireValidScene(json.is_number_integer());
        if (json.is_number_unsigned())
            RequireValidScene(json.template get<uint64>() <=
                              static_cast<uint64>(std::numeric_limits<T>::max()));
        else {
            const auto value = json.template get<int64>();
            if constexpr (std::is_unsigned_v<T>)
                RequireValidScene(value >= 0 &&
                                  static_cast<uint64>(value) <= std::numeric_limits<T>::max());
            else
                RequireValidScene(value >= std::numeric_limits<T>::min() &&
                                  value <= std::numeric_limits<T>::max());
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        RequireValidScene(json.is_number());
        const auto value = json.template get<double>();
        RequireValidScene(std::isfinite(value) && std::abs(value) <= std::numeric_limits<T>::max());
    } else
        RequireValidScene(json.is_string());
    output = json.template get<T>();
}
void ReadJsonValue(const Json& json, Vec2& value) {
    RequireValidScene(json.is_array() && json.size() == 2);
    ReadJsonValue(json[0], value.x);
    ReadJsonValue(json[1], value.y);
}
void ReadJsonValue(const Json& json, Vec3& value) {
    RequireValidScene(json.is_array() && json.size() == 3);
    ReadJsonValue(json[0], value.x);
    ReadJsonValue(json[1], value.y);
    ReadJsonValue(json[2], value.z);
}
void ReadJsonValue(const Json& json, Vec4& value) {
    RequireValidScene(json.is_array() && json.size() == 4);
    ReadJsonValue(json[0], value.x);
    ReadJsonValue(json[1], value.y);
    ReadJsonValue(json[2], value.z);
    ReadJsonValue(json[3], value.w);
}
void ReadJsonValue(const Json& json, BodyType& value) {
    int number;
    ReadJsonValue(json, number);
    RequireValidScene(number >= 0 && number <= 2);
    value = static_cast<BodyType>(number);
}
void ReadJsonValue(const Json& json, SceneReferenceMode& value) {
    int number;
    ReadJsonValue(json, number);
    RequireValidScene(number >= 0 && number <= 2);
    value = static_cast<SceneReferenceMode>(number);
}
void ReadJsonValue(const Json& json, Render3DMode& value) {
    int number;
    ReadJsonValue(json, number);
    RequireValidScene(number >= 0 && number <= 1);
    value = static_cast<Render3DMode>(number);
}
void ReadJsonValue(const Json& json, std::vector<uint8_t>& tiles) {
    RequireValidScene(json.is_array() && json.size() <= 256 * 256);
    tiles.clear();
    tiles.reserve(json.size());
    for (const auto& item : json) {
        uint8_t number;
        ReadJsonValue(item, number);
        RequireValidScene(number <= 5);
        tiles.push_back(number);
    }
}
void ReadJsonValue(const Json& json, std::vector<Keyframe>& keys) {
    RequireValidScene(json.is_array() && json.size() <= 4096);
    keys.clear();
    keys.reserve(json.size());
    float last = -1;
    for (const auto& item : json) {
        Keyframe key;
        ReadJsonValue(item.at("time"), key.time);
        RequireValidScene(key.time >= 0 && key.time > last);
        last = key.time;
        ReadJsonValue(item.at("position"), key.position);
        ReadJsonValue(item.at("rotation"), key.rotation);
        ReadJsonValue(item.at("scale"), key.scale);
        keys.push_back(key);
    }
}
void ReadJsonValue(const Json& json, std::vector<std::pair<uint32, uint32>>& map) {
    RequireValidScene(json.is_array() && json.size() <= MaxEntities);
    map.clear();
    map.reserve(json.size());
    for (const auto& pair : json) {
        RequireValidScene(pair.is_array() && pair.size() == 2);
        uint32 a, b;
        ReadJsonValue(pair[0], a);
        ReadJsonValue(pair[1], b);
        map.emplace_back(a, b);
    }
}

Json ParseSceneJson(const std::string& data) {
    // Bound nesting before building the DOM and reject duplicate keys instead of silently losing
    // data.
    std::vector<std::set<std::string>> objectKeys;
    return Json::parse(data, [&](int depth, Json::parse_event_t event, Json& parsed) {
        RequireValidScene(depth <= 64);
        if (event == Json::parse_event_t::object_start)
            objectKeys.emplace_back();
        else if (event == Json::parse_event_t::key)
            RequireValidScene(!objectKeys.empty() &&
                              objectKeys.back().insert(parsed.get<std::string>()).second);
        else if (event == Json::parse_event_t::object_end)
            objectKeys.pop_back();
        return true;
    });
}
} // namespace

bool Scene::SerializeToString(std::string& output) {
    try {
        if (!ValidateForSerialization())
            return false;
        Json root = {{"format", "LuxEngine.Scene"}, {"version", 1}};
        Json settings = Json::object();
        settings["clear_color"] = ToJson(settings_.clearColor);
        settings["exposure"] = ToJson(settings_.exposure);
        settings["gravity2d"] = ToJson(settings_.gravity);
        settings["gravity3d"] = ToJson(settings_.gravity3D);
        settings["override_gravity"] = ToJson(settings_.overrideGravity);
        settings["fixed_step"] = ToJson(settings_.fixedStep);
        settings["physics_substeps"] = ToJson(settings_.physicsSubsteps);
        Json render = Json::object();
        render["mode"] = ToJson(settings_.render3D.mode);
        render["sun_direction"] = ToJson(settings_.render3D.sun.direction);
        render["sun_color"] = ToJson(settings_.render3D.sun.color);
        render["sun_intensity"] = ToJson(settings_.render3D.sun.intensity);
        render["environment_color"] = ToJson(settings_.render3D.environmentColor);
        render["environment_intensity"] = ToJson(settings_.render3D.environmentIntensity);
        render["environment_rotation"] = ToJson(settings_.render3D.environmentRotation);
        render["environment"] = ToJson(settings_.render3D.environmentPath);
        render["image_based_lighting"] = ToJson(settings_.render3D.imageBasedLighting);
        render["skybox"] = ToJson(settings_.render3D.showSkybox);
        render["tone_mapping"] = ToJson(settings_.render3D.toneMapping);
        render["shadows"] = ToJson(settings_.render3D.shadows);
        render["shadow_resolution"] = ToJson(settings_.render3D.shadowResolution);
        render["shadow_range"] = ToJson(settings_.render3D.shadowRange);
        render["shadow_depth_bias"] = ToJson(settings_.render3D.shadowDepthBias);
        render["shadow_normal_bias"] = ToJson(settings_.render3D.shadowNormalBias);
        settings["render3d"] = std::move(render);
        root["settings"] = std::move(settings);
        root["entities"] = Json::array();
        std::vector<uint32> ids(entities_.begin(), entities_.end());
        std::sort(ids.begin(), ids.end());
        for (auto id : ids) {
            Json components = Json::object();
            if (const auto* component = TryGetComponent<TagComponent>(id)) {
                Json value = Json::object();
                value["name"] = ToJson(component->tag);
                components["tag"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<TransformComponent>(id)) {
                Json value = Json::object();
                value["position"] = ToJson(component->position);
                value["rotation"] = ToJson(component->rotation);
                value["scale"] = ToJson(component->scale);
                components["transform"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<SpriteRendererComponent>(id)) {
                Json value = Json::object();
                value["color"] = ToJson(component->color);
                components["sprite"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<MeshRendererComponent>(id)) {
                Json value = Json::object();
                value["albedo"] = ToJson(component->material.albedoColor);
                value["metallic"] = ToJson(component->material.metallic);
                value["roughness"] = ToJson(component->material.roughness);
                value["asset"] = ToJson(component->meshPath);
                value["base_color_texture"] = ToJson(component->material.baseColorTexture);
                components["mesh"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<CameraComponent>(id)) {
                Json value = Json::object();
                value["primary"] = ToJson(component->isPrimary);
                value["perspective"] = ToJson(component->isPerspective);
                value["fov"] = ToJson(component->fov);
                value["ortho_size"] = ToJson(component->orthoSize);
                components["camera"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<Rigidbody2DComponent>(id)) {
                Json value = Json::object();
                value["type"] = ToJson(component->type);
                value["velocity"] = ToJson(component->velocity);
                value["mass"] = ToJson(component->mass);
                value["gravity_scale"] = ToJson(component->gravityScale);
                value["grounded"] = ToJson(component->isGrounded);
                components["rigidbody2d"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<BoxCollider2DComponent>(id)) {
                Json value = Json::object();
                value["size"] = ToJson(component->size);
                value["offset"] = ToJson(component->offset);
                value["trigger"] = ToJson(component->isTrigger);
                components["box_collider2d"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<Rigidbody3DComponent>(id)) {
                Json value = Json::object();
                value["type"] = ToJson(component->type);
                value["velocity"] = ToJson(component->velocity);
                value["mass"] = ToJson(component->mass);
                value["gravity_scale"] = ToJson(component->gravityScale);
                components["rigidbody3d"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<BoxCollider3DComponent>(id)) {
                Json value = Json::object();
                value["size"] = ToJson(component->size);
                value["offset"] = ToJson(component->offset);
                value["trigger"] = ToJson(component->isTrigger);
                components["box_collider3d"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<TilemapComponent>(id)) {
                Json value = Json::object();
                value["width"] = ToJson(component->width);
                value["height"] = ToJson(component->height);
                value["tile_size"] = ToJson(component->tileSize);
                value["tiles"] = ToJson(component->tiles);
                components["tilemap"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<AnimationComponent>(id)) {
                Json value = Json::object();
                value["name"] = ToJson(component->clip.name);
                value["duration"] = ToJson(component->clip.duration);
                value["loop"] = ToJson(component->clip.loop);
                value["playing"] = ToJson(component->playing);
                value["keyframes"] = ToJson(component->clip.keyframes);
                components["animation"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<HierarchyComponent>(id)) {
                Json value = Json::object();
                value["parent"] = ToJson(component->parent);
                value["group"] = ToJson(component->group);
                components["hierarchy"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<SceneReferenceComponent>(id)) {
                Json value = Json::object();
                value["path"] = ToJson(component->path);
                value["mode"] = ToJson(component->mode);
                value["baseline"] = ToJson(component->baseline);
                value["suspended"] = ToJson(component->suspended);
                value["stream_center"] = ToJson(component->streamCenter);
                value["load_radius"] = ToJson(component->streamLoadRadius);
                value["unload_radius"] = ToJson(component->streamUnloadRadius);
                value["loaded"] = ToJson(component->loaded);
                value["entity_map"] = ToJson(component->entityMap);
                components["scene_reference"] = std::move(value);
            }
            if (const auto* component = TryGetComponent<InstanceSourceComponent>(id)) {
                Json value = Json::object();
                value["root"] = ToJson(component->instanceRoot);
                value["entity"] = ToJson(component->sourceEntity);
                components["instance_source"] = std::move(value);
            }
            root["entities"].push_back({{"id", id}, {"components", std::move(components)}});
        }
        auto serialized = root.dump(2) + '\n';
        if (serialized.size() > MaxSceneBytes)
            return false;
        output = std::move(serialized);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool Scene::DeserializeJson(const std::string& data) {
    try {
        const auto root = ParseSceneJson(data);
        RequireValidScene(root.is_object() && root.at("format") == "LuxEngine.Scene");
        uint32 version = 0;
        ReadJsonValue(root.at("version"), version);
        RequireValidScene(version == 1);
        Scene parsed;
        SceneSettings settings;
        const auto& source = root.at("settings");
        ReadJsonValue(source.at("clear_color"), settings.clearColor);
        ReadJsonValue(source.at("exposure"), settings.exposure);
        ReadJsonValue(source.at("gravity2d"), settings.gravity);
        ReadJsonValue(source.at("gravity3d"), settings.gravity3D);
        ReadJsonValue(source.at("override_gravity"), settings.overrideGravity);
        ReadJsonValue(source.at("fixed_step"), settings.fixedStep);
        ReadJsonValue(source.at("physics_substeps"), settings.physicsSubsteps);
        const auto& render = source.at("render3d");
        ReadJsonValue(render.at("mode"), settings.render3D.mode);
        ReadJsonValue(render.at("sun_direction"), settings.render3D.sun.direction);
        ReadJsonValue(render.at("sun_color"), settings.render3D.sun.color);
        ReadJsonValue(render.at("sun_intensity"), settings.render3D.sun.intensity);
        ReadJsonValue(render.at("environment_color"), settings.render3D.environmentColor);
        ReadJsonValue(render.at("environment_intensity"), settings.render3D.environmentIntensity);
        ReadJsonValue(render.at("environment_rotation"), settings.render3D.environmentRotation);
        ReadJsonValue(render.at("environment"), settings.render3D.environmentPath);
        ReadJsonValue(render.at("image_based_lighting"), settings.render3D.imageBasedLighting);
        ReadJsonValue(render.at("skybox"), settings.render3D.showSkybox);
        ReadJsonValue(render.at("tone_mapping"), settings.render3D.toneMapping);
        ReadJsonValue(render.at("shadows"), settings.render3D.shadows);
        ReadJsonValue(render.at("shadow_resolution"), settings.render3D.shadowResolution);
        ReadJsonValue(render.at("shadow_range"), settings.render3D.shadowRange);
        ReadJsonValue(render.at("shadow_depth_bias"), settings.render3D.shadowDepthBias);
        ReadJsonValue(render.at("shadow_normal_bias"), settings.render3D.shadowNormalBias);
        RequireValidScene(parsed.SetSettings(settings));
        const auto& entities = root.at("entities");
        RequireValidScene(entities.is_array() && entities.size() <= MaxEntities);
        std::set<uint32> ids;
        for (const auto& item : entities) {
            uint32 id = 0;
            ReadJsonValue(item.at("id"), id);
            RequireValidScene(id > 0 && id < std::numeric_limits<uint32>::max() &&
                              ids.insert(id).second);
            parsed.nextEntityId_ = id;
            auto entity = parsed.CreateEntity();
            entity.RemoveComponent<TagComponent>();
            entity.RemoveComponent<TransformComponent>();
            const auto& components = item.at("components");
            RequireValidScene(components.is_object());
            for (const auto& [name, value] : components.items()) {
                if (name == "tag") {
                    TagComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("name"), component.tag);
                    entity.AddComponent<TagComponent>(std::move(component));
                } else if (name == "transform") {
                    TransformComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("position"), component.position);
                    ReadJsonValue(value.at("rotation"), component.rotation);
                    ReadJsonValue(value.at("scale"), component.scale);
                    entity.AddComponent<TransformComponent>(std::move(component));
                } else if (name == "sprite") {
                    SpriteRendererComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("color"), component.color);
                    entity.AddComponent<SpriteRendererComponent>(std::move(component));
                } else if (name == "mesh") {
                    MeshRendererComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("albedo"), component.material.albedoColor);
                    ReadJsonValue(value.at("metallic"), component.material.metallic);
                    ReadJsonValue(value.at("roughness"), component.material.roughness);
                    ReadJsonValue(value.at("asset"), component.meshPath);
                    ReadJsonValue(value.at("base_color_texture"),
                                  component.material.baseColorTexture);
                    entity.AddComponent<MeshRendererComponent>(std::move(component));
                } else if (name == "camera") {
                    CameraComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("primary"), component.isPrimary);
                    ReadJsonValue(value.at("perspective"), component.isPerspective);
                    ReadJsonValue(value.at("fov"), component.fov);
                    ReadJsonValue(value.at("ortho_size"), component.orthoSize);
                    entity.AddComponent<CameraComponent>(std::move(component));
                } else if (name == "rigidbody2d") {
                    Rigidbody2DComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("type"), component.type);
                    ReadJsonValue(value.at("velocity"), component.velocity);
                    ReadJsonValue(value.at("mass"), component.mass);
                    ReadJsonValue(value.at("gravity_scale"), component.gravityScale);
                    ReadJsonValue(value.at("grounded"), component.isGrounded);
                    entity.AddComponent<Rigidbody2DComponent>(std::move(component));
                } else if (name == "box_collider2d") {
                    BoxCollider2DComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("size"), component.size);
                    ReadJsonValue(value.at("offset"), component.offset);
                    ReadJsonValue(value.at("trigger"), component.isTrigger);
                    entity.AddComponent<BoxCollider2DComponent>(std::move(component));
                } else if (name == "rigidbody3d") {
                    Rigidbody3DComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("type"), component.type);
                    ReadJsonValue(value.at("velocity"), component.velocity);
                    ReadJsonValue(value.at("mass"), component.mass);
                    ReadJsonValue(value.at("gravity_scale"), component.gravityScale);
                    entity.AddComponent<Rigidbody3DComponent>(std::move(component));
                } else if (name == "box_collider3d") {
                    BoxCollider3DComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("size"), component.size);
                    ReadJsonValue(value.at("offset"), component.offset);
                    ReadJsonValue(value.at("trigger"), component.isTrigger);
                    entity.AddComponent<BoxCollider3DComponent>(std::move(component));
                } else if (name == "tilemap") {
                    TilemapComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("width"), component.width);
                    ReadJsonValue(value.at("height"), component.height);
                    ReadJsonValue(value.at("tile_size"), component.tileSize);
                    ReadJsonValue(value.at("tiles"), component.tiles);
                    entity.AddComponent<TilemapComponent>(std::move(component));
                } else if (name == "animation") {
                    AnimationComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("name"), component.clip.name);
                    ReadJsonValue(value.at("duration"), component.clip.duration);
                    ReadJsonValue(value.at("loop"), component.clip.loop);
                    ReadJsonValue(value.at("playing"), component.playing);
                    ReadJsonValue(value.at("keyframes"), component.clip.keyframes);
                    entity.AddComponent<AnimationComponent>(std::move(component));
                } else if (name == "hierarchy") {
                    HierarchyComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("parent"), component.parent);
                    ReadJsonValue(value.at("group"), component.group);
                    entity.AddComponent<HierarchyComponent>(std::move(component));
                } else if (name == "scene_reference") {
                    SceneReferenceComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("path"), component.path);
                    ReadJsonValue(value.at("mode"), component.mode);
                    ReadJsonValue(value.at("baseline"), component.baseline);
                    ReadJsonValue(value.at("suspended"), component.suspended);
                    ReadJsonValue(value.at("stream_center"), component.streamCenter);
                    ReadJsonValue(value.at("load_radius"), component.streamLoadRadius);
                    ReadJsonValue(value.at("unload_radius"), component.streamUnloadRadius);
                    ReadJsonValue(value.at("loaded"), component.loaded);
                    ReadJsonValue(value.at("entity_map"), component.entityMap);
                    entity.AddComponent<SceneReferenceComponent>(std::move(component));
                } else if (name == "instance_source") {
                    InstanceSourceComponent component;
                    RequireValidScene(value.is_object());
                    ReadJsonValue(value.at("root"), component.instanceRoot);
                    ReadJsonValue(value.at("entity"), component.sourceEntity);
                    entity.AddComponent<InstanceSourceComponent>(std::move(component));
                } else
                    return false; // Unknown components must not disappear during the next save.
            }
        }
        parsed.nextEntityId_ = ids.empty() ? 1 : *ids.rbegin() + 1;
        if (!parsed.ValidateForSerialization())
            return false;
        AdoptLoadedScene(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
} // namespace LuxEngine
