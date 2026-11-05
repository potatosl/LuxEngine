cbuffer Camera : register(b0, space1) { column_major float4x4 viewProjection; };
struct Input { float3 position : TEXCOORD0; float4 color : TEXCOORD1; float2 uv : TEXCOORD2; };
struct Output { float4 position : SV_Position; float4 color : TEXCOORD0; float2 uv : TEXCOORD1; };
Output main(Input input) {
    Output output;
    output.position = mul(viewProjection, float4(input.position, 1));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}
