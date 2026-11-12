struct SceneData {
    float4 camera;
    float4 environment;
    float4 options;
    float4 right;
    float4 up;
    float4 forward;
    column_major float4x4 shadowMatrix;
    float4 shadowOptions;
};
[[vk::binding(0, 2)]] ConstantBuffer<SceneData> scene;
[[vk::binding(1, 2)]] TextureCube diffuseEnvironment;
[[vk::binding(2, 2)]] TextureCube specularEnvironment;
[[vk::binding(3, 2)]] TextureCube skyEnvironment;
[[vk::binding(4, 2)]] Texture2D brdfLut;
[[vk::binding(5, 2)]] Texture2D<float> shadowImage;
[[vk::binding(6, 2)]] SamplerState environmentSampler;

float3 RotateEnvironment(float3 v) {
    const float c = cos(scene.options.x), s = sin(scene.options.x);
    return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}
float3 EnvironmentLighting(float3 base, float metal, float rough, float3 n, float3 v) {
    const float nv = saturate(dot(n, v));
    const float3 f0 = lerp(0.04, base, metal);
    if (scene.options.y < 0.5)
        return base * (1 - metal) * scene.environment.rgb * scene.environment.w;
    const float3 irradiance =
        diffuseEnvironment.SampleLevel(environmentSampler, RotateEnvironment(n), 0).rgb;
    const float3 reflected =
        specularEnvironment
            .SampleLevel(environmentSampler, RotateEnvironment(reflect(-v, n)), rough * 7)
            .rgb;
    const float2 brdf = brdfLut.SampleLevel(environmentSampler, float2(nv, rough), 0).rg;
    const float3 f = f0 + (max(1 - rough, f0) - f0) * pow(1 - nv, 5);
    return ((1 - f) * (1 - metal) * base * irradiance + reflected * (f0 * brdf.x + brdf.y)) *
           scene.environment.w;
}

float DirectionalShadow(float3 world, float3 normal) {
    if (scene.shadowOptions.x < 0.5)
        return 1;
    const float4 clip = mul(scene.shadowMatrix, float4(world + normal * scene.shadowOptions.w, 1));
    const float3 p = clip.xyz / clip.w;
    const float2 uv = float2(p.x * .5 + .5, .5 - p.y * .5);
    if (any(uv < 0) || any(uv > 1) || p.z < 0 || p.z > 1)
        return 1;
    const int resolution = (int)round(1 / scene.shadowOptions.y);
    const int2 center = (int2)(uv * resolution);
    float visibility = 0;
    [unroll] for (int y = -1; y <= 1; ++y)[unroll] for (int x = -1; x <= 1; ++x) {
        const int2 pixel = clamp(center + int2(x, y), 0, resolution - 1);
        visibility += p.z - scene.shadowOptions.z <= shadowImage.Load(int3(pixel, 0)) ? 1 : 0;
    }
    return visibility / 9;
}
