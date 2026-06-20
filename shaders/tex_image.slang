[[vk::binding(0, 0)]] Sampler2D displayTexture;

[shader("fragment")]
void fragmentMain(
    float3 inColor,
    float2 inUV,
    out float4 outFragColor : SV_Target0
) {
    outFragColor = displayTexture.Sample(inUV);
}