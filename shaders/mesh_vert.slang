#include "input_structures.slang"

struct Vertex {
    float3 position;
    float uv_x;
    float3 normal;
    float uv_y;
    float4 color;
};

struct PushConstantsT {
    float4x4 render_matrix;
    Vertex* vertexBuffer;
};

[[vk::push_constant]]
ConstantBuffer<PushConstantsT> PushConstants;

[shader("vertex")]
void main(
    uint gl_VertexIndex : SV_VertexID,
    out [[vk::location(0)]] float3 outNormal,
    out [[vk::location(1)]] float3 outColor,
    out [[vk::location(2)]] float2 outUV,
    out float4 gl_Position : SV_Position
) {
    Vertex v = PushConstants.vertexBuffer[gl_VertexIndex];
    
    float4 position = float4(v.position, 1.0f);

    gl_Position = mul(mul(sceneData.viewproj, PushConstants.render_matrix), position);

    outNormal = mul(PushConstants.render_matrix, float4(v.normal, 0.f)).xyz;
    outColor = v.color.xyz * materialData.colorFactors.xyz;
    outUV.x = v.uv_x;
    outUV.y = v.uv_y;
}
