using LuxEngine;

namespace ScriptTests {
    public sealed class Mover : EntityScript {
        protected override void OnCreate() {
            Transform value = Entity.Transform;
            value.Position.Y += 10;
            Entity.Transform = value;
        }

        protected override void OnUpdate(double deltaTime) {
            Transform value = Entity.Transform;
            value.Position.X += (float)deltaTime;
            Entity.Transform = value;
        }

        protected override void OnDestroy() {
            if (Entity.IsValid) {
                Transform value = Entity.Transform;
                value.Position.Z += 1;
                Entity.Transform = value;
            }
        }
    }

    public sealed class Throwing : EntityScript {
        protected override void OnUpdate(double deltaTime) {
            throw new InvalidOperationException("managed regression exception");
        }
    }
}
