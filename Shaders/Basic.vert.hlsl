struct Camera {
    column_major float4x4 viewProjection;
};
[[vk::push_constant]] ConstantBuffer<Camera> camera;
struct Object {
    column_major float4x4 model;
    float4 albedo;
    float4 colorOptions;
};
[[vk::binding(0, 1)]] ConstantBuffer<Object> object;
struct Input {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float3 normal : NORMAL0;
};
struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 world : TEXCOORD1;
    [[vk::location(3)]] float3 normal : TEXCOORD2;
    [[vk::location(4)]] nointerpolation float4 options : TEXCOORD3;
};
Output main(Input input) {
    Output output;
    const float4 world = mul(object.model, float4(input.position, 1));
    output.position = mul(camera.viewProjection, world);
    output.color = input.color * object.albedo;
    output.uv = input.uv;
    output.world = world.xyz;
    output.options = object.colorOptions;
    const float3x3 model = (float3x3)object.model;
    const float3x3 cofactors =
        float3x3(cross(model[1], model[2]), cross(model[2], model[0]), cross(model[0], model[1]));
    const float determinant = dot(model[0], cofactors[0]);
    output.normal =
        abs(determinant) > 1e-10 ? mul(cofactors, input.normal) / determinant : float3(0, 0, 0);
    return output;
}
