struct SceneData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;
    float4 ambientColor;
    float4 sunlightDirection; //w for sun power
    float4 sunlightColor;
};
[[vk::binding(0, 0)]] ConstantBuffer<SceneData> sceneData;

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
    out [[vk::location(0)]] float3 outNormal,
    out [[vk::location(1)]] float2 outTexCoords,
    out float4 gl_Position : SV_Position
) {
    Vertex currentVertex = PushConstants.vertexBuffer[gl_VertexIndex];

    float4 initialPos = float4(currentVertex.position, 1.0f);

    gl_Position = mul(mul(sceneData.viewproj, PushConstants.modelMatrix), initialPos);
    outNormal = mul(PushConstants.modelMatrix, float4(currentVertex.normal, 0.0f)).xyz;
    outTexCoords = float2(currentVertex.uv_x, currentVertex.uv_y);
}
