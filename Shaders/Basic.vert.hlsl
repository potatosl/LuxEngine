struct Camera { column_major float4x4 viewProjection; };
[[vk::push_constant]] ConstantBuffer<Camera> camera;
struct Input {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};
struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};
Output main(Input input) {
    Output output;
    output.position = mul(camera.viewProjection, float4(input.position, 1));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}
