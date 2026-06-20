//glsl version 4.5



struct SceneData {   
    float4 fogColor; // w is for exponent
	float4 fogDistances; //x for min, y for max, zw unused.
	float4 ambientColor;
	float4 sunlightDirection; //w for sun power
	float4 sunlightColor;
};
[[vk::binding(1, 0)]] ConstantBuffer<SceneData> sceneData;


[shader("fragment")]
void main(
	in float3 inColor,
	out float4 outFragColor : SV_Target0
) 
{	
	outFragColor = float4(inColor + sceneData.ambientColor.xyz,1.0f);
}