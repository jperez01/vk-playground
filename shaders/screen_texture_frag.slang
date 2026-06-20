[[vk::binding(1, 0)]] Sampler2D colorTex;

[shader("fragment")]
void main(
    float2 inTexCoords,
    out float4 outFragColor : SV_Target0
) {
    float3 color = colorTex.Sample(inTexCoords).xyz;

    outFragColor = float4(inTexCoords, 1.0f, 1.0f);
}