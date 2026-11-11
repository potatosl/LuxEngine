[[vk::binding(0, 0)]] Texture2D image;
[[vk::binding(1, 0)]] SamplerState imageSampler;
[[vk::constant_id(0)]] const bool lit = false;
struct PointLightData {
    float4 positionRadius;
    float4 colorIntensity;
};
struct Lights {
    float4 direction;
    float4 colorIntensity;
    uint4 count;
    PointLightData points[16];
};
[[vk::binding(1, 1)]] ConstantBuffer<Lights> lights;
struct Input {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 world : TEXCOORD1;
    [[vk::location(3)]] float3 normal : TEXCOORD2;
    [[vk::location(4)]] nointerpolation float encodeSrgb : TEXCOORD3;
};
float4 main(Input input) : SV_Target0 {
    float4 color = input.color * image.Sample(imageSampler, input.uv);
    if (lit && input.encodeSrgb > 0.5 && color.a == 0.0)
        discard;
    if (lit) {
        const float3 faceNormal = dot(input.normal, input.normal) > 1e-10
                                      ? input.normal : cross(ddy(input.world), ddx(input.world));
        const float lenSq = dot(faceNormal, faceNormal);
        const float3 normal = lenSq > 1e-10 ? faceNormal * rsqrt(lenSq) : float3(0.0, 1.0, 0.0);
        float3 light = lights.colorIntensity.rgb * (max(0.0, dot(normal, -lights.direction.xyz)) *
                                                    max(0.0, lights.colorIntensity.w));
        for (uint index = 0; index < min(lights.count.x, 16u); ++index) {
            const PointLightData source = lights.points[index];
            const float3 toLight = source.positionRadius.xyz - input.world;
            const float distance = length(toLight);
            if (source.positionRadius.w <= 0.0 || distance >= source.positionRadius.w)
                continue;
            const float facing =
                distance > 0.00001 ? max(0.0, dot(normal, toLight / distance)) : 1.0;
            const float falloff = 1.0 - distance / source.positionRadius.w;
            light += source.colorIntensity.rgb *
                     (facing * falloff * falloff * max(0.0, source.colorIntensity.w));
        }
        color.rgb *= 0.15 + light;
    }
    if (lit && input.encodeSrgb > 0.5) {
        const float3 linearColor = max(color.rgb, 0.0);
        color.rgb = lerp(12.92 * linearColor, 1.055 * pow(linearColor, 1.0 / 2.4) - 0.055,
                         step(0.0031308, linearColor));
    }
    return color;
}
