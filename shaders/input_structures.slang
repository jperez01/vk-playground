struct SceneData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;
    float4 ambientColor;
    float4 sunlightDirection;
    float4 sunlightColor;
};

[[vk::binding(0, 0)]] ConstantBuffer<SceneData> sceneData;

#ifdef USE_BINDLESS
[[vk::binding(1, 0)]] Sampler2D allTextures[];
#else
[[vk::binding(1, 1)]] Sampler2D colorTex;
[[vk::binding(2, 1)]] Sampler2D metalRoughTex;
#endif

struct GLTFMaterialData {
    float4 colorFactors;
    float4 metal_rough_factors;
    int colorTexID;
    int metalRoughTexID;
};

[[vk::binding(0, 1)]] ConstantBuffer<GLTFMaterialData> materialData;
