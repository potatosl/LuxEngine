using System.Numerics;
using System.Runtime.InteropServices;

namespace LuxEngine {

    [StructLayout(LayoutKind.Sequential)]
    public struct Transform {
        public Vector3 Position;
        public Vector3 Rotation;
        public Vector3 Scale;
    }

    public sealed class Entity {
        private readonly ulong world;
        public uint Id {
            get;
        }

        internal Entity(ulong world, uint id) {
            this.world = world;
            Id = id;
        }

        public bool IsValid {
            get {
                return NativeBridge.IsAlive(world, Id);
            }
        }

        public Transform Transform {
            get {
                return NativeBridge.GetTransform(world, Id);
            }
            set {
                NativeBridge.SetTransform(world, Id, value);
            }
        }
    }

    public abstract class EntityScript {
        public Entity Entity {
            get; internal set;
        } = null!;

        protected virtual void OnCreate() {
        }

        protected virtual void OnUpdate(double deltaTime) {
        }

        protected virtual void OnDestroy() {
        }

        internal void Create() {
            OnCreate();
        }

        internal void Update(double deltaTime) {
            OnUpdate(deltaTime);
        }

        internal void Destroy() {
            OnDestroy();
        }
    }

    public static class Input {
        public static bool IsKeyPressed(LuxKey key) {
            return NativeBridge.IsKeyPressed((int)key);
        }
    }

    // These are engine key codes, matching the public C++ LuxKey API.
    public enum LuxKey {
        A = 'a', B = 'b', C = 'c', D = 'd', E = 'e', F = 'f', G = 'g', H = 'h',
        I = 'i', J = 'j', K = 'k', L = 'l', M = 'm', N = 'n', O = 'o', P = 'p',
        Q = 'q', R = 'r', S = 's', T = 't', U = 'u', V = 'v', W = 'w', X = 'x', Y = 'y', Z = 'z',
        Space = 32, Enter = 13, Escape = 27,
        Right = 0x4000004f, Left = 0x40000050, Down = 0x40000051, Up = 0x40000052
    }
}
