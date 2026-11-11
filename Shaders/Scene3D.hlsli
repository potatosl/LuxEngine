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
