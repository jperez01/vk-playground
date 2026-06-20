struct CameraBuffer {   
    float4x4 view;
    float4x4 proj;
	float4x4 viewproj; 
};
[[vk::binding(0, 0)]] ConstantBuffer<CameraBuffer> cameraData;

struct ObjectData{
	float4x4 model;
}; 

//all object matrices
[[vk::binding(0, 1)]] StructuredBuffer<ObjectData> objectBuffer;

//push constants block
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
	float2 vTexCoord,
	uint gl_BaseInstance : SV_InstanceID,
	out float3 outColor,
	out float2 texCoord,
	out float4 gl_Position : SV_Position
) 
{	
	float4x4 modelMatrix = objectBuffer[gl_BaseInstance].model;
	float4x4 transformMatrix = mul(cameraData.viewproj, modelMatrix);
	gl_Position = mul(transformMatrix, float4(vPosition, 1.0f));
	outColor = vColor;
	texCoord = vTexCoord;
}