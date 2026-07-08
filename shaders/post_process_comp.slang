[[vk::binding(0, 0)]] 
Texture2D<float4> drawImage;

[[vk::binding(1, 0)]] 
RWTexture2D<float4> postProcessingImage;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    int2 texelCoord = int2(threadId.xy);
    
    // Bounds check to avoid writing outside the image
    uint width, height;
    postProcessingImage.GetDimensions(width, height);
    if (texelCoord.x >= width || texelCoord.y >= height) { return; }

    // Read HDR color
    float3 color = drawImage[texelCoord].rgb;

    // 1. Tone Mapping (Reinhard)
    color = color / (color + 1.0f);

    // 2. Gamma Correction (converts to sRGB for the UNORM swapchain)
    color = pow(color, 1.0f / 2.2f);

    // Write LDR color
    postProcessingImage[texelCoord] = float4(color, 1.0f);
}