Texture2D image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);
struct Input { float4 position : SV_Position; float4 color : TEXCOORD0; float2 uv : TEXCOORD1; };
float4 main(Input input) : SV_Target0 {
    return input.color * image.Sample(imageSampler, input.uv);
}
