struct SceneData {   

	float4x4 view;
	float4x4 proj;
	float4x4 viewproj;
	float4 ambientColor;
	float4 sunlightDirection; //w for sun power
	float4 sunlightColor;
};
[[vk::binding(0, 1)]] ConstantBuffer<SceneData> sceneData;