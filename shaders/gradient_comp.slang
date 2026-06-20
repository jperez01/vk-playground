


[[vk::binding(0, 0)]] RWTexture2D<float4> image;


[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 gl_GlobalInvocationID : SV_DispatchThreadID, uint3 gl_LocalInvocationID : SV_GroupThreadID) 
{
    int2 texelCoord = int2(gl_GlobalInvocationID.xy);
    
    uint width, height;
    image.GetDimensions(width, height);
    int2 size = int2(width, height);

    if(texelCoord.x < size.x && texelCoord.y < size.y)
    {
        float4 color = float4(0.0, 0.0, 0.0, 1.0);        

        if(gl_LocalInvocationID.x != 0 && gl_LocalInvocationID.y != 0)
        {
            color.x = float(texelCoord.x)/(size.x);
            color.y = float(texelCoord.y)/(size.y);	
        }
    
        image[texelCoord] = color;
    }
}

