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
    float4 camera;
    PointLightData points[16];
};
[[vk::binding(1, 1)]] ConstantBuffer<Lights> lights;
struct Input {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 world : TEXCOORD1;
    [[vk::location(3)]] float3 normal : TEXCOORD2;
    [[vk::location(4)]] nointerpolation float4 options : TEXCOORD3;
};
float3 SafeDirection(float3 v, float3 fallback) {
    return dot(v, v) > 1e-10 ? v * rsqrt(dot(v, v)) : fallback;
}
float3 Fresnel(float cosine, float3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - saturate(cosine), 5.0);
}
float3 Brdf(float3 base, float metal, float rough, float3 n, float3 v, float3 l) {
    const float3 h = SafeDirection(v + l, n);
    const float nl = saturate(dot(n, l));
    const float nv = max(saturate(dot(n, v)), 1e-4);
    const float nh = saturate(dot(n, h));
    const float vh = saturate(dot(v, h));
    const float a = rough * rough;
    const float a2 = a * a;
    const float d = nh * nh * (a2 - 1.0) + 1.0;
    const float distribution = a2 / max(3.14159265 * d * d, 1e-10);
    const float visibility = 0.5 / max(nl * sqrt(nv * nv * (1.0 - a2) + a2) +
                                      nv * sqrt(nl * nl * (1.0 - a2) + a2), 1e-5);
    const float3 f = Fresnel(vh, lerp(0.04, base, metal));
    return ((1.0 - f) * (1.0 - metal) * base / 3.14159265 +
            distribution * visibility * f) * nl;
}
float4 main(Input input) : SV_Target0 {
    float4 color = input.color * image.Sample(imageSampler, input.uv);
    if (lit && input.options.x > 0.5 && color.a == 0.0)
        discard;
    if (lit) {
        const float3 faceNormal = dot(input.normal, input.normal) > 1e-10
                                      ? input.normal : cross(ddy(input.world), ddx(input.world));
        const float lenSq = dot(faceNormal, faceNormal);
        const float3 normal = lenSq > 1e-10 ? faceNormal * rsqrt(lenSq) : float3(0.0, 1.0, 0.0);
        if (input.options.w > 0.5) {
            const float3 v = SafeDirection(lights.camera.xyz - input.world, normal);
            const float metal = saturate(input.options.y);
            const float rough = clamp(input.options.z, 0.045, 1.0);
            float3 result = Brdf(color.rgb, metal, rough, normal, v, -lights.direction.xyz) *
                            lights.colorIntensity.rgb * max(lights.colorIntensity.w, 0.0);
            for (uint i = 0; i < min(lights.count.x, 16u); ++i) {
                const PointLightData source = lights.points[i];
                const float3 delta = source.positionRadius.xyz - input.world;
                const float distance = length(delta);
                const float falloff = saturate(1.0 - distance / max(source.positionRadius.w, 1e-5));
                result += Brdf(color.rgb, metal, rough, normal, v,
                               SafeDirection(delta, normal)) * source.colorIntensity.rgb *
                          max(source.colorIntensity.w, 0.0) * falloff * falloff;
            }
            color.rgb = result;
        } else {
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
    }
    if (lit && (input.options.x > 0.5 || input.options.w > 0.5)) {
        const float3 linearColor = max(color.rgb, 0.0);
        color.rgb = lerp(12.92 * linearColor, 1.055 * pow(linearColor, 1.0 / 2.4) - 0.055,
                         step(0.0031308, linearColor));
    }
    return color;
}
