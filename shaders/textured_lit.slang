struct SceneData {   
    float4 fogColor; // w is for exponent
	float4 fogDistances; //x for min, y for max, zw unused.
	float4 ambientColor;
	float4 sunlightDirection; //w for sun power
	float4 sunlightColor;
};
[[vk::binding(1, 0)]] ConstantBuffer<SceneData> sceneData;

[[vk::binding(0, 2)]] Sampler2D tex1;

[shader("fragment")]
void fragmentMain(
    float3 inColor,
    float2 texCoord,
    out float4 outFragColor : SV_Target0
) {
    float3 color = tex1.Sample(texCoord).xyz;
    outFragColor = float4(color, 1.0f);
}