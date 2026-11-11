struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
Output main(uint id : SV_VertexID) {
    Output output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * 2 - 1, 0, 1);
    return output;
}
