[[vk::binding(0, 0)]] Texture2D hdrImage;
[[vk::binding(1, 0)]] SamplerState hdrSampler;
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
[[vk::binding(2, 0)]] ConstantBuffer<SceneData> scene;
struct Input {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
float4 main(Input input) : SV_Target0 {
    float4 color = hdrImage.SampleLevel(hdrSampler, float2(input.uv.x, 1 - input.uv.y), 0);
    if (color.a <= 0.0)
        discard;
    color.rgb = max(color.rgb * scene.camera.w, 0);
    if (scene.options.z > 0.5)
        color.rgb = saturate((color.rgb * (2.51 * color.rgb + 0.03)) /
                             (color.rgb * (2.43 * color.rgb + 0.59) + 0.14));
    color.rgb = lerp(12.92 * color.rgb, 1.055 * pow(color.rgb, 1.0 / 2.4) - 0.055,
                     step(0.0031308, color.rgb));
    return color;
}
