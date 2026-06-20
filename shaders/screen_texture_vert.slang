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

[shader("vertex")]
void main(
    uint gl_VertexIndex : SV_VertexID,
    out float2 outTexCoords,
    out float4 gl_Position : SV_Position
) {
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

    outTexCoords = float2(currentUv.x, currentUv.y);
    gl_Position = float4(currentPosition.x, currentPosition.y, 1.0, 1.0);
}