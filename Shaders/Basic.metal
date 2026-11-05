#include <metal_stdlib>
using namespace metal;
struct Input { float3 position [[attribute(0)]]; float4 color [[attribute(1)]]; float2 uv [[attribute(2)]]; };
struct Output { float4 position [[position]]; float4 color; float2 uv; };
vertex Output vertexMain(Input input [[stage_in]], constant float4x4& vp [[buffer(0)]]) {
    return {vp * float4(input.position, 1), input.color, input.uv};
}
fragment float4 fragmentMain(Output input [[stage_in]], texture2d<float> image [[texture(0)]], sampler s [[sampler(0)]]) {
    return input.color * image.sample(s, input.uv);
}
