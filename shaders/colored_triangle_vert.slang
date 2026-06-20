//> all



[shader("vertex")]
void main(
	uint gl_VertexIndex : SV_VertexID,
	out float3 outColor,
	out float4 gl_Position : SV_Position
) 
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

	//output the position of each vertex
	gl_Position = float4(positions[gl_VertexIndex], 1.0f);
	outColor = colors[gl_VertexIndex];
}
//< all