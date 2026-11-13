using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace LuxEngine {

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct NativeApi {
        public uint Version;
        public uint Size;
        public delegate* unmanaged[Cdecl]<ulong, uint, int> IsAlive;
        public delegate* unmanaged[Cdecl]<ulong, uint, Transform*, int> GetTransform;
        public delegate* unmanaged[Cdecl]<ulong, uint, Transform*, int> SetTransform;
        public delegate* unmanaged[Cdecl]<int, int> IsKeyPressed;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct ManagedApi {
        public uint Version;
        public uint Size;
        public delegate* unmanaged[Cdecl]<ulong, byte*, int, ulong> Load;
        public delegate* unmanaged[Cdecl]<ulong, uint, byte*, int, int> Attach;
        public delegate* unmanaged[Cdecl]<ulong, uint, int> Detach;
        public delegate* unmanaged[Cdecl]<ulong, double, int> Update;
        public delegate* unmanaged[Cdecl]<ulong, void> Unload;
        public delegate* unmanaged[Cdecl]<byte*, int, int> LastError;
    }

    internal sealed class ScriptAssembly : AssemblyLoadContext {
        private readonly AssemblyDependencyResolver resolver;

        internal ScriptAssembly(string path) : base(isCollectible: true) {
            resolver = new AssemblyDependencyResolver(path);
        }

        protected override Assembly? Load(AssemblyName name) {
            if (name.Name == typeof(EntityScript).Assembly.GetName().Name)
                return typeof(EntityScript).Assembly;
            string? path = resolver.ResolveAssemblyToPath(name);
            return path is null ? null : LoadFromAssemblyPath(path);
        }

        protected override nint LoadUnmanagedDll(string name) {
            string? path = resolver.ResolveUnmanagedDllToPath(name);
            return path is null ? 0 : LoadUnmanagedDllFromPath(path);
        }
    }

    // This is the only managed/native entry surface. Exceptions never cross its C ABI.
    public static unsafe class NativeBridge {
        private sealed class World {
            internal required ulong NativeId;
            internal required ScriptAssembly Loader;
            internal required Assembly Assembly;
            internal readonly SortedDictionary<uint, EntityScript> Instances = new();
        }

        private static NativeApi native;
        private static readonly Dictionary<ulong, World> worlds = new();
        private static ulong nextId;
        private static string lastError = string.Empty;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int GetApi(void* nativeApi, void* managedApi) {
            try {
                if (nativeApi == null || managedApi == null)
                    return 0;
                NativeApi callbacks = *(NativeApi*)nativeApi;
                if (callbacks.Version != 1 || callbacks.Size != sizeof(NativeApi))
                    return 0;
                native = callbacks;
                *(ManagedApi*)managedApi = new ManagedApi {
                    Version = 1,
                    Size = (uint)sizeof(ManagedApi),
                    Load = &Load,
                    Attach = &Attach,
                    Detach = &Detach,
                    Update = &Update,
                    Unload = &Unload,
                    LastError = &LastError
                };
                return 1;
            } catch (Exception error) {
                lastError = error.ToString();
                return 0;
            }
        }

        internal static bool IsAlive(ulong world, uint entity) {
            return native.IsAlive(world, entity) != 0;
        }

        internal static Transform GetTransform(ulong world, uint entity) {
            Transform value;
            if (native.GetTransform(world, entity, &value) == 0)
                throw new InvalidOperationException("The entity is unavailable or the script is running off the scene thread.");
            return value;
        }

        internal static void SetTransform(ulong world, uint entity, Transform value) {
            if (native.SetTransform(world, entity, &value) == 0)
                throw new InvalidOperationException("The entity transform is invalid or unavailable.");
        }

        internal static bool IsKeyPressed(int key) {
            return native.IsKeyPressed(key) != 0;
        }

        private static World Find(ulong id) {
            if (!worlds.TryGetValue(id, out World? world)) {
                throw new InvalidOperationException("The script world has stopped.");
            }
            return world;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ulong Load(ulong nativeWorld, byte* path, int length) {
            ScriptAssembly? loader = null;
            try {
                string assemblyPath = Path.GetFullPath(Encoding.UTF8.GetString(new ReadOnlySpan<byte>(path, length)));
                loader = new ScriptAssembly(assemblyPath);
                Assembly assembly = loader.LoadFromAssemblyPath(assemblyPath);
                ulong id = ++nextId;
                worlds.Add(id, new World { NativeId = nativeWorld, Loader = loader, Assembly = assembly });
                return id;
            } catch (Exception error) {
                loader?.Unload();
                lastError = error.ToString();
                return 0;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static int Attach(ulong id, uint entity, byte* name, int length) {
            try {
                World world = Find(id);
                string typeName = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(name, length));
                Type type = world.Assembly.GetType(typeName, throwOnError: true)!;
                if (type.IsAbstract || !typeof(EntityScript).IsAssignableFrom(type))
                    throw new InvalidOperationException($"{typeName} must be a concrete EntityScript class.");
                if (world.Instances.ContainsKey(entity))
                    throw new InvalidOperationException("A script is already attached to this entity.");
                EntityScript script = (EntityScript)Activator.CreateInstance(type)!;
                script.Entity = new Entity(world.NativeId, entity);
                script.Create();
                world.Instances.Add(entity, script);
                return 1;
            } catch (Exception error) {
                lastError = error.ToString();
                return 0;
            }
        }

        private static void Remove(World world, uint entity) {
            if (!world.Instances.Remove(entity, out EntityScript? script))
                return;
            script.Destroy();
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static int Detach(ulong id, uint entity) {
            try {
                Remove(Find(id), entity);
                return 1;
            } catch (Exception error) {
                lastError = error.ToString();
                return 0;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static int Update(ulong id, double deltaTime) {
            try {
                World world = Find(id);
                foreach ((uint entity, EntityScript script) in world.Instances.ToArray()) {
                    if (!IsAlive(world.NativeId, entity))
                        Remove(world, entity);
                    else
                        script.Update(deltaTime);
                }
                return 1;
            } catch (Exception error) {
                lastError = error.ToString();
                return 0;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static void Unload(ulong id) {
            if (!worlds.Remove(id, out World? world))
                return;
            try {
                foreach (uint entity in world.Instances.Keys.ToArray()) {
                    try {
                        Remove(world, entity);
                    } catch (Exception error) { lastError = error.ToString(); }
                }
            } finally {
                world.Instances.Clear();
                world.Loader.Unload();
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static int LastError(byte* destination, int capacity) {
            try {
                byte[] bytes = Encoding.UTF8.GetBytes(lastError);
                if (destination != null && capacity > 0)
                    bytes.AsSpan(0, Math.Min(bytes.Length, capacity - 1)).CopyTo(new Span<byte>(destination, capacity));
                if (destination != null && capacity > 0)
                    destination[Math.Min(bytes.Length, capacity - 1)] = 0;
                return bytes.Length;
            } catch { return 0; }
        }
    }
}
