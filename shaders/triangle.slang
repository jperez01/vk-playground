struct VertexOutput {
    float4 gl_Position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID) {
    VertexOutput output;
    const float3 positions[3] = {
        float3(1.f,1.f, 0.0f),
        float3(-1.f,1.f, 0.0f),
        float3(0.f,-1.f, 0.0f)
    };
    output.gl_Position = float4(positions[vertexIndex], 1.0f);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    return float4(1.f,0.f,0.f,1.0f);
}