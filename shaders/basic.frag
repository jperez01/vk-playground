#version 460

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoords;

layout(location = 0) out vec4 outFragColor;

layout(set = 1, binding = 1) uniform sampler2D colorTex;

void main() {
    vec3 color = texture(colorTex, inTexCoords).xyz;

    outFragColor = vec4(color, 1.0f);
}