//> all



[shader("fragment")]
void main(
	in float3 inColor,
	out float4 outFragColor : SV_Target0
) 
{
	//return red
	outFragColor = float4(inColor,1.0f);
}
//< all