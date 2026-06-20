


#include "input_structures.slang"


struct SHCoefficients {
    float3 l00, l1m1, l10, l11, l2m2, l2m1, l20, l21, l22;
};

static const SHCoefficients grace = SHCoefficients(
    float3( 0.3623915,  0.2624130,  0.2326261 ),
    float3( 0.1759131,  0.1436266,  0.1260569 ),
    float3(-0.0247311, -0.0101254, -0.0010745 ),
    float3( 0.0346500,  0.0223184,  0.0101350 ),
    float3( 0.0198140,  0.0144073,  0.0043987 ),
    float3(-0.0469596, -0.0254485, -0.0117786 ),
    float3(-0.0898667, -0.0760911, -0.0740964 ),
    float3( 0.0050194,  0.0038841,  0.0001374 ),
    float3(-0.0818750, -0.0321501,  0.0033399 )
);

float3 calcIrradiance(float3 nor) {
    const SHCoefficients c = grace;
    const float c1 = 0.429043;
    const float c2 = 0.511664;
    const float c3 = 0.743125;
    const float c4 = 0.886227;
    const float c5 = 0.247708;
    return (
        c1 * c.l22 * (nor.x * nor.x - nor.y * nor.y) +
        c3 * c.l20 * nor.z * nor.z +
        c4 * c.l00 -
        c5 * c.l20 +
        2.0 * c1 * c.l2m2 * nor.x * nor.y +
        2.0 * c1 * c.l21  * nor.x * nor.z +
        2.0 * c1 * c.l2m1 * nor.y * nor.z +
        2.0 * c2 * c.l11  * nor.x +
        2.0 * c2 * c.l1m1 * nor.y +
        2.0 * c2 * c.l10  * nor.z
    );
}

[shader("fragment")]
void main(
    float3 inNormal,
    float3 inColor,
    float2 inUV,
    out float4 outFragColor : SV_Target0) 
{
	float lightValue = max(dot(inNormal, float3(0.3f,1.f,0.3f)), 0.1f);

	float3 irradiance = calcIrradiance(inNormal); 


	float3 color = inColor * colorTex.Sample(inUV).xyz;

	outFragColor = float4(color * lightValue + color * irradiance.x * float3(0.2f) ,1.0f);
}

