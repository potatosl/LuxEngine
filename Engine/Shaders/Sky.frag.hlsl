#include "Scene3D.hlsli"
struct Input {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
float4 main(Input input) : SV_Target0 {
    const float2 p = input.uv * 2 - 1;
    const float3 direction =
        normalize(scene.forward.xyz + p.x * scene.right.xyz + p.y * scene.up.xyz);
    return float4(
        skyEnvironment.SampleLevel(environmentSampler, RotateEnvironment(direction), 0).rgb *
            scene.environment.w,
        1);
}
