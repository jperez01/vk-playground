struct CameraBuffer {   
    float4x4 view;
    float4x4 proj;
	float4x4 viewproj;
};
[[vk::binding(0, 0)]] ConstantBuffer<CameraBuffer> cameraData;

struct constants {
 float4 data;
 float4x4 render_matrix;
};
[[vk::push_constant]] ConstantBuffer<constants> PushConstants;

[shader("vertex")]
void vertexMain(
    float3 vPosition,
    float3 vNormal,
    float3 vColor,
    out float3 outColor,
    out float4 gl_Position : SV_Position
) 
{	
	float4x4 transformMatrix = mul(cameraData.viewproj, PushConstants.render_matrix);
	gl_Position = mul(transformMatrix, float4(vPosition, 1.0f));
	outColor = vColor;
}