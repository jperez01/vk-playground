#version 460

#extension GL_EXT_buffer_reference : require

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outTexCoords;

layout(set = 0, binding = 0) uniform  SceneData{

    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 ambientColor;
    vec4 sunlightDirection; //w for sun power
    vec4 sunlightColor;
} sceneData;

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout (buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout( push_constant ) uniform constants {
    mat4 modelMatrix;
    VertexBuffer vertexBuffer;
} PushConstants;

void main() {
    Vertex currentVertex = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

    vec4 initialPos = vec4(currentVertex.position, 1.0f);

    gl_Position = sceneData.viewproj * PushConstants.modelMatrix * initialPos;
    outNormal = (PushConstants.modelMatrix * vec4(currentVertex.normal, 0.0f)).xyz;
    outTexCoords = vec2(currentVertex.uv_x, currentVertex.uv_y);
}