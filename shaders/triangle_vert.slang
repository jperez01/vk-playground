[shader("vertex")]
float4 main(uint vertexIndex : SV_VertexID) : SV_Position {
    const float3 positions[3] = {
        float3(1.f,1.f, 0.0f),
        float3(-1.f,1.f, 0.0f),
        float3(0.f,-1.f, 0.0f)
    };
    return float4(positions[vertexIndex], 1.0f);
}
