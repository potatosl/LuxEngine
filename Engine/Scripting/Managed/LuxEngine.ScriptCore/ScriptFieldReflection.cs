using System.Numerics;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace LuxEngine {
    [AttributeUsage(AttributeTargets.Field)]
    public sealed class SerializeFieldAttribute : Attribute {
    }

    internal static class ScriptFieldReflection {
        private static int Kind(Type type) {
            if (type == typeof(bool)) return 0;
            if (type == typeof(int)) return 1;
            if (type == typeof(float)) return 2;
            if (type == typeof(string)) return 3;
            if (type == typeof(Vector2)) return 4;
            if (type == typeof(Vector3)) return 5;
            if (type == typeof(Vector4)) return 6;
            if (type == typeof(Entity)) return 7;
            throw new InvalidOperationException($"Unsupported serialized script field type {type.FullName}.");
        }

        private static SortedDictionary<string, FieldInfo> Fields(Type type) {
            var fields = new SortedDictionary<string, FieldInfo>(StringComparer.Ordinal);
            for (Type? current = type; current != null && current != typeof(EntityScript); current = current.BaseType) {
                foreach (FieldInfo field in current.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly)) {
                    bool marked = field.IsDefined(typeof(SerializeFieldAttribute), inherit: false);
                    if ((!field.IsPublic && !marked) || field.IsStatic || field.IsInitOnly || field.IsLiteral)
                        continue;
                    _ = Kind(field.FieldType);
                    if (!fields.TryAdd(field.Name, field))
                        throw new InvalidOperationException($"Serialized script field {field.Name} is hidden by another field.");
                }
            }
            if (fields.Count > 128)
                throw new InvalidOperationException("A script supports at most 128 serialized fields.");
            return fields;
        }

        private static float Number(JsonElement value) {
            float number = value.GetSingle();
            if (!float.IsFinite(number)) throw new InvalidOperationException("Script field values must be finite.");
            return number;
        }

        private static object? Read(JsonElement value, int kind, ulong world) {
            if (kind >= 4 && kind <= 6 && value.GetArrayLength() != kind - 2)
                throw new InvalidOperationException("Script vector field has an incorrect size.");
            return kind switch {
                0 => value.GetBoolean(),
                1 => value.GetInt32(),
                2 => Number(value),
                3 => value.GetString() ?? string.Empty,
                4 => new Vector2(Number(value[0]), Number(value[1])),
                5 => new Vector3(Number(value[0]), Number(value[1]), Number(value[2])),
                6 => new Vector4(Number(value[0]), Number(value[1]), Number(value[2]), Number(value[3])),
                7 => value.GetUInt32() == 0 ? null : new Entity(world, value.GetUInt32()),
                _ => throw new InvalidOperationException("Unsupported script field type.")
            };
        }

        internal static void Apply(EntityScript script, string json, ulong world) {
            if (json.Length > 65536) throw new InvalidOperationException("Script field data exceeds the limit.");
            using JsonDocument document = JsonDocument.Parse(json);
            var fields = Fields(script.GetType());
            var updates = new List<(FieldInfo Field, object? Value)>();
            foreach (JsonProperty entry in document.RootElement.EnumerateObject()) {
                if (!fields.TryGetValue(entry.Name, out FieldInfo? field))
                    throw new InvalidOperationException($"Unknown serialized script field {entry.Name}.");
                int kind = Kind(field.FieldType);
                if (entry.Value.GetProperty("type").GetInt32() != kind)
                    throw new InvalidOperationException($"Serialized script field type changed: {entry.Name}.");
                updates.Add((field, Read(entry.Value.GetProperty("value"), kind, world)));
            }
            foreach (var update in updates)
                update.Field.SetValue(script, update.Value);
        }

        internal static string Describe(Type type) {
            EntityScript instance = (EntityScript)Activator.CreateInstance(type)!;
            var json = new JsonObject();
            foreach (var (name, field) in Fields(type)) {
                object? value = field.GetValue(instance);
                JsonNode? node = value switch {
                    Vector2 vector => new JsonArray(vector.X, vector.Y),
                    Vector3 vector => new JsonArray(vector.X, vector.Y, vector.Z),
                    Vector4 vector => new JsonArray(vector.X, vector.Y, vector.Z, vector.W),
                    Entity entity => JsonValue.Create(entity.Id),
                    null when field.FieldType == typeof(Entity) => JsonValue.Create(0u),
                    null => JsonValue.Create(string.Empty),
                    _ => JsonSerializer.SerializeToNode(value, field.FieldType)
                };
                json[name] = new JsonObject { ["type"] = Kind(field.FieldType), ["value"] = node };
            }
            return json.ToJsonString();
        }
    }
}
