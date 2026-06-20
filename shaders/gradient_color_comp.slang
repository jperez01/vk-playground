


[[vk::binding(0, 0)]] RWTexture2D<float4> image;

//push constants block
struct constants {
 float4 data1;
 float4 data2;
 float4 data3;
 float4 data4;
};
[[vk::push_constant]] ConstantBuffer<constants> PushConstants;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 gl_GlobalInvocationID : SV_DispatchThreadID) 
{
    int2 texelCoord = int2(gl_GlobalInvocationID.xy);

    uint width, height;
    image.GetDimensions(width, height);
    int2 size = int2(width, height);

    float4 topColor = PushConstants.data1;
    float4 bottomColor = PushConstants.data2;

    if(texelCoord.x < size.x && texelCoord.y < size.y)
    {
        float blend = float(texelCoord.y)/(size.y); 
    
        image[texelCoord] = lerp(topColor,bottomColor, blend);
    }
}

