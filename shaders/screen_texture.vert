#version 460

#extension GL_EXT_buffer_reference : require

layout (location = 0) out vec2 outTexCoords;

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout (push_constant) uniform constants {
    mat4 modelMatrix;
    VertexBuffer vertexBuffer;
} PushConstants;

void main() {
    vec3 positions[] = {
        vec3(1.0, -1.0, 0.0),
        vec3(1.0,1.0, 0),
        vec3(-1.0,-1.0, 0.0),
        vec3(-1.0,1.0, 0 )
    };

    vec2 uvs[] = {
            vec2(1, 0),
        vec2(0, 0),
        vec2(1, 1),
        vec2(0, 1)
    };

    Vertex current = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

    vec2 currentUv  = uvs[gl_VertexIndex].xy;
    vec3 currentPosition = positions[gl_VertexIndex].xyz;

    outTexCoords = vec2(currentUv.x, currentUv.y);
    gl_Position = vec4(currentPosition.x, currentPosition.y, 1.0, 1.0);
}