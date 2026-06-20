struct constants {
 float4 data;
 float4x4 render_matrix;
};
[[vk::push_constant]] ConstantBuffer<constants> PushConstants;

[shader("vertex")]
void main(
	float3 vPosition,
	float3 vNormal,
	float3 vColor,
	out float3 outColor,
	out float4 gl_Position : SV_Position
) 
{	
	gl_Position = mul(PushConstants.render_matrix, float4(vPosition, 1.0f));
	outColor = vColor;
}