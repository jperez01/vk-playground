struct SceneData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;
    float4 ambientColor;
    float4 sunlightDirection; //w for sun power
    float4 sunlightColor;
};
[[vk::binding(0, 0)]] ConstantBuffer<SceneData> sceneData;

[[vk::binding(1, 1)]] Sampler2D colorTex;
[[vk::binding(2, 1)]] Sampler2D normalTex;

[shader("fragment")]
void main(
    [[vk::location(0)]] float3 inNormal,
    [[vk::location(1)]] float2 inTexCoords,
    out float4 outFragColor : SV_Target0
) {
    float3 color = colorTex.Sample(inTexCoords).xyz;
    float3 normal = normalTex.Sample(inTexCoords).xyz;

    float3 lightDir = -sceneData.sunlightDirection.xyz;
    float multiplier = dot(lightDir, normalize(inNormal));

    float3 someColor = multiplier * color * sceneData.sunlightColor.xyz;

    outFragColor = float4(someColor, 1.0f);
}
