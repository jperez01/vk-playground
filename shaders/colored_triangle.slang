//> all

struct VertexOutput {
	[[vk::location(0)]] float3 outColor;
	float4 gl_Position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint gl_VertexIndex : SV_VertexID) 
{
	//const array of positions for the triangle
	const float3 positions[3] = {
		float3(1.f,1.f, 0.0f),
		float3(-1.f,1.f, 0.0f),
		float3(0.f,-1.f, 0.0f)
	};

	//const array of colors for the triangle
	const float3 colors[3] = {
		float3(1.0f, 0.0f, 0.0f), //red
		float3(0.0f, 1.0f, 0.0f), //green
		float3(0.0f, 0.0f, 1.0f)  //blue
	};

	VertexOutput output;
	//output the position of each vertex
	output.gl_Position = float4(positions[gl_VertexIndex], 1.0f);
	output.outColor = colors[gl_VertexIndex];
	
	return output;
}

[shader("fragment")]
void fragmentMain(
	VertexOutput input,
	out float4 outFragColor : SV_Target0
) 
{
	//return red
	outFragColor = float4(input.outColor,1.0f);
}
//< all