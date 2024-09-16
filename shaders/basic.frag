#version 460

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoords;

layout(location = 0) out vec4 outFragColor;

layout(set = 0, binding = 0) uniform  SceneData{

    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 ambientColor;
    vec4 sunlightDirection; //w for sun power
    vec4 sunlightColor;
} sceneData;

layout(set = 1, binding = 1) uniform sampler2D colorTex;
layout(set = 1, binding = 2) uniform sampler2D normalTex;

void main() {
    vec3 color = texture(colorTex, inTexCoords).xyz;
    vec3 normal = texture(normalTex, inTexCoords).xyz;

    vec3 lightDir = -sceneData.sunlightDirection.xyz;
    float multiplier = dot(lightDir, normalize(inNormal));

    vec3 someColor = multiplier * color * sceneData.sunlightColor.xyz;

    outFragColor = vec4(someColor, 1.0f);
}