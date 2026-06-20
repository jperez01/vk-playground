struct SceneData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;
    float4 ambientColor;
    float4 sunlightDirection;
    float4 sunlightColor;
};

[[vk::binding(0, 0)]] ConstantBuffer<SceneData> sceneData;


struct GLTFMaterialData {
    float4 colorFactors;
    float4 metal_rough_factors;
    int colorTexID;
    int metalRoughTexID;
};

[[vk::binding(1, 0)]] Sampler2D globalTextures[];
