// GPU frustum culling: one thread per draw-surface slot.
// Layouts mirror GPUCullObject / GPUCullPushConstants in shared/vk_types.h; change both together.

struct CullObject {
    float4 center;      // xyz: world-space box center
    float4 halfExtents; // xyz: nonnegative world-space half-sizes
};

struct CullParams {
    float4 planes[6];   // xyz: inward normal, w: offset (unnormalized, same order as the CPU planes)
    uint objectCount;   // valid slots this dispatch
    uint padding0;
    uint padding1;
    uint padding2;
};

// Same five 32-bit fields as VkDrawIndexedIndirectCommand (stride 20 bytes).
struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

[[vk::binding(0, 0)]] StructuredBuffer<CullObject> cullObjects;
[[vk::binding(1, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> drawCommands;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> cullStats; // [0] visible surfaces, [1] visible triangles
[[vk::push_constant]] ConstantBuffer<CullParams> params;

// Mirrors learning::isBoxPotentiallyVisible: reject only when one plane excludes the whole box.
bool isBoxPotentiallyVisible(float3 center, float3 halfExtents)
{
    for (int i = 0; i < params.planes.getCount(); i++) {
        var plane = params.planes[i];

        var centerValue = dot(plane.xyz, center) + plane.w;
        var radius = dot(abs(plane.xyz), halfExtents);

        var largestValue = centerValue + radius;
        if (largestValue < 0) return false;
    }
    return true;
}

// Workgroup size 64: dispatch ceil(objectCount / 64) groups.
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint slot = threadId.x;
    // The last group is usually partial; extra threads must not touch memory.
    if (slot >= params.objectCount) { return; }

    CullObject object = cullObjects[slot];
    bool visible = isBoxPotentiallyVisible(object.center.xyz, object.halfExtents.xyz);
    // Every slot is written every dispatch, so a hidden surface never keeps a stale instanceCount = 1.
    drawCommands[slot].instanceCount = visible ? 1u : 0u;
    if (visible) {
        uint previous;
        InterlockedAdd(cullStats[0], 1u, previous);
        InterlockedAdd(cullStats[1], drawCommands[slot].indexCount / 3u, previous);
    }
}
