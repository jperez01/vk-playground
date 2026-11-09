#include "input_structures.slang"

struct GuideConstants {
    float4x4 inverseViewProjection;
    float4 viewportMode;
};
[[vk::push_constant]] ConstantBuffer<GuideConstants> guides;

struct GuideVertex {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 ndc;
    [[vk::location(1)]] noperspective float edge;
    [[vk::location(2)]] float3 color;
};

// Intersect an unbounded world-space line with one homogeneous clip half-space.
bool clipAxis(float origin, float direction, inout float first, inout float last)
{
    if (direction == 0.0) return origin >= 0.0;
    float t = -origin / direction;
    if (direction > 0.0) first = max(first, t);
    else last = min(last, t);
    return first <= last;
}

float gridCoverage(float2 world, float2 footprint, float spacing)
{
    float2 distance = abs(frac(world / spacing + 0.5) - 0.5) * spacing;
    float2 coverage = saturate(1.0 - distance / footprint);
    // Fade each line family before it becomes subpixel-dense, avoiding a bright horizon.
    coverage *= smoothstep(2.0, 8.0, spacing / footprint);
    return max(coverage.x, coverage.y);
}

[shader("vertex")]
GuideVertex vertexMain(uint id : SV_VertexID)
{
    GuideVertex output = (GuideVertex)0;
    if (guides.viewportMode.z < 0.5) {
        float2 xy = float2((id << 1) & 2, id & 2) * 2.0 - 1.0;
        output.position = float4(xy, 0.0, 1.0);
        output.ndc = xy;
        return output;
    }

    // Clip P(t) = origin + t * direction, with no world-space endpoint limit.
    uint axis = id / 6;
    uint corner = id % 6;
    float3 direction = axis == 0 ? float3(1, 0, 0) : (axis == 1 ? float3(0, 1, 0) : float3(0, 0, 1));
    float4 origin = mul(sceneData.viewproj, float4(0, 0, 0, 1));
    float4 vector = mul(sceneData.viewproj, float4(direction, 0));
    float first = -3.402823e38;
    float last = 3.402823e38;
    // Vulkan clip volume: -W <= X,Y <= W and 0 <= Z <= W (reversed-Z).
    bool visible = clipAxis(origin.w + origin.x, vector.w + vector.x, first, last);
    visible = clipAxis(origin.w - origin.x, vector.w - vector.x, first, last) && visible;
    visible = clipAxis(origin.w + origin.y, vector.w + vector.y, first, last) && visible;
    visible = clipAxis(origin.w - origin.y, vector.w - vector.y, first, last) && visible;
    visible = clipAxis(origin.z, vector.z, first, last) && visible;
    visible = clipAxis(origin.w - origin.z, vector.w - vector.z, first, last) && visible;
    if (!visible) {
        output.position = float4(0, 0, 0, 1);
        return output;
    }
    float4 a = origin + first * vector;
    float4 b = origin + last * vector;
    float2 delta = (b.xy / b.w - a.xy / a.w) * guides.viewportMode.xy;
    float lengthSquared = dot(delta, delta);
    if (lengthSquared < 0.0001) {
        output.position = float4(0, 0, 0, 1);
        return output;
    }
    float2 normal = float2(-delta.y, delta.x) * rsqrt(lengthSquared);
    bool end = corner == 2 || corner == 4 || corner == 5;
    float side = (corner == 0 || corner == 3 || corner == 5) ? -1.0 : 1.0;
    float4 clip = end ? b : a;
    clip.xy += normal * side * 2.0 / guides.viewportMode.xy * clip.w;
    output.position = clip;
    output.edge = side;
    output.color = axis == 0 ? float3(0.9, 0.06, 0.04) : (axis == 1 ? float3(0.08, 0.8, 0.12) : float3(0.06, 0.25, 1.0));
    return output;
}

[shader("fragment")]
void fragmentMain(GuideVertex input, out float4 color : SV_Target0, out float depth : SV_Depth)
{
    if (guides.viewportMode.z > 0.5) {
        color = float4(input.color, saturate(1.0 - abs(input.edge)));
        depth = input.position.z;
        return;
    }

    float4 nearH = mul(guides.inverseViewProjection, float4(input.ndc, 1.0, 1.0));
    float4 farH = mul(guides.inverseViewProjection, float4(input.ndc, 0.0, 1.0));
    float3 nearPoint = nearH.xyz / nearH.w;
    float3 farPoint = farH.xyz / farH.w;
    float3 ray = farPoint - nearPoint;
    if (abs(ray.y) < 0.00001) discard;
    float t = -nearPoint.y / ray.y;
    if (t <= 0.0 || t >= 1.0) discard;
    float3 world = nearPoint + ray * t;

    // Euclidean screen derivatives keep diagonal lines as thin as horizontal ones.
    float2 dx = ddx(world.xz);
    float2 dy = ddy(world.xz);
    float2 footprint = max(sqrt(dx * dx + dy * dy), float2(0.0001));
    float minor = gridCoverage(world.xz, footprint, 1.0);
    float major = gridCoverage(world.xz, footprint, 10.0);
    float fade = 1.0 - smoothstep(25.0, 100.0, length(world - nearPoint));
    float coverage = max(minor * 0.45, major * 0.8) * fade;
    if (coverage < 0.001) discard;
    float4 clip = mul(sceneData.viewproj, float4(world, 1.0));
    depth = clip.z / clip.w;
    if (depth < 0.0 || depth > 1.0) discard;
    color = float4(0.12, 0.12, 0.12, coverage);
}
