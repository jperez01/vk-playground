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

[[vk::binding(1, 1)]] Sampler2D colorTex;
[[vk::binding(2, 1)]] Sampler2D normalTex;

struct VertexOutput {
    [[vk::location(0)]] float3 outNormal;
    [[vk::location(1)]] float2 outTexCoords;
    float4 gl_Position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint gl_VertexIndex : SV_VertexID) {
    VertexOutput output;
    Vertex currentVertex = PushConstants.vertexBuffer[gl_VertexIndex];

    float4 initialPos = float4(currentVertex.position, 1.0f);

    output.gl_Position = mul(mul(sceneData.viewproj, PushConstants.modelMatrix), initialPos);
    output.outNormal = mul(PushConstants.modelMatrix, float4(currentVertex.normal, 0.0f)).xyz;
    output.outTexCoords = float2(currentVertex.uv_x, currentVertex.uv_y);
    
    return output;
}

[shader("fragment")]
void fragmentMain(
    VertexOutput input,
    out float4 outFragColor : SV_Target0
) {
    float3 color = colorTex.Sample(input.outTexCoords).xyz;
    float3 normal = normalTex.Sample(input.outTexCoords).xyz;

    float3 lightDir = -sceneData.sunlightDirection.xyz;
    float multiplier = dot(lightDir, normalize(input.outNormal));

    float3 someColor = multiplier * color * sceneData.sunlightColor.xyz;

    outFragColor = float4(someColor, 1.0f);
}