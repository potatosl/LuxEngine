struct Camera {
    column_major float4x4 viewProjection;
};
[[vk::push_constant]] ConstantBuffer<Camera> camera;
struct Object {
    column_major float4x4 model;
    float4 albedo;
    float4 options;
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
};
Output main(Input input) {
    Output output;
    output.position = mul(camera.viewProjection, mul(object.model, float4(input.position, 1)));
    output.color = input.color * object.albedo;
    output.uv = input.uv;
    return output;
}
