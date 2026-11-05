[[vk::binding(0, 0)]] Texture2D image;
[[vk::binding(1, 0)]] SamplerState imageSampler;
struct Input {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};
float4 main(Input input) : SV_Target0 {
    return input.color * image.Sample(imageSampler, input.uv);
}
