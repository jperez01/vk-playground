//> all




struct Vertex {

	float3 position;
	float uv_x;
	float3 normal;
	float uv_y;
	float4 color;
}; 


//push constants block
struct constants {	
	float4x4 render_matrix;
	Vertex* vertexBuffer;
};
[[vk::push_constant]] ConstantBuffer<constants> PushConstants;

[shader("vertex")]
void vertexMain(
	uint gl_VertexIndex : SV_VertexID,
	out float3 outColor,
	out float2 outUV,
	out float4 gl_Position : SV_Position
) 
{	
	//load vertex data from device adress
	Vertex v = PushConstants.vertexBuffer[gl_VertexIndex];

	//output data
	gl_Position = mul(PushConstants.render_matrix, float4(v.position, 1.0f));
	outColor = v.color.xyz;
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;
}
//< all
