struct Vertex {
    float3 position;
    float uv_x;
    float3 normal;
    float uv_y;
    float4 color;
};

struct constants {
    float4x4 modelMatrix;
    Vertex* vertexBuffer;
};
[[vk::push_constant]] ConstantBuffer<constants> PushConstants;

struct VertexOutput {
    [[vk::location(0)]] float2 outTexCoords;
    float4 gl_Position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint gl_VertexIndex : SV_VertexID) {
    VertexOutput output;
    float3 positions[] = {
        float3(1.0, -1.0, 0.0),
        float3(1.0, 1.0, 0),
        float3(-1.0, -1.0, 0.0),
        float3(-1.0, 1.0, 0)
    };

    float2 uvs[] = {
        float2(1, 0),
        float2(0, 0),
        float2(1, 1),
        float2(0, 1)
    };

    Vertex current = PushConstants.vertexBuffer[gl_VertexIndex];

    float2 currentUv = uvs[gl_VertexIndex].xy;
    float3 currentPosition = positions[gl_VertexIndex].xyz;

    output.outTexCoords = float2(currentUv.x, currentUv.y);
    output.gl_Position = float4(currentPosition.x, currentPosition.y, 1.0, 1.0);
    
    return output;
}

[[vk::binding(1, 0)]] Sampler2D colorTex;

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0 {
    float3 color = colorTex.Sample(input.outTexCoords).xyz;

    return float4(input.outTexCoords, 1.0f, 1.0f);
}