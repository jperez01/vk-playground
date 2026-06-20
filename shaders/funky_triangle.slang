

// Created by inigo quilez - iq/2013
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

// Some geometric orbit traps in the Mandelbrot set. More info:
//
// https://iquilezles.org/www/articles/ftrapsgeometric/ftrapsgeometric.htm

// antialiasing level (squared)
#define AA 3


void mainImage( out float4 fragColor, in float2 fragCoord )
{
	float2 iResolution = float2(1700.f,900.f);
	float iTime = 0;
    float3 col = float3(0.0);
    
    for( int m=0; m<AA; m++ )
    for( int n=0; n<AA; n++ )
    {
        
    float2 p = (2.0*(fragCoord+float2(float(m),float(n))/float(AA))-iResolution.xy) / iResolution.y;

	float zoo = 1.0/(400.0 - 150.0*sin(0.15*iTime-0.3));
	
	float2 cc = float2(-0.533516,0.526141) + p*zoo;

	float2 t2c = float2(-0.5,2.0);
	t2c += 0.5*float2( cos(0.13*(iTime-10.0)), sin(0.13*(iTime-10.0)) );
		
    // iterate
    float2 z  = float2(0.0);
    float2 dz = float2(0.0);
	float trap1 = 0.0;
	float trap2 = 1e20;
	float co2 = 0.0;
    for( int i=0; i<150; i++ )
    {
        if( dot(z,z)>1024.0 ) break;

		// Z' -> 2·Z·Z' + 1
        dz = 2.0*float2(z.x*dz.x-z.y*dz.y, z.x*dz.y + z.y*dz.x ) + float2(1.0,0.0);
			
        // Z -> Z² + c			
        z = cc + float2( z.x*z.x - z.y*z.y, 2.0*z.x*z.y );
			
        // trap 1
		float d1 = abs(dot(z-float2(0.0,1.0),float2(0.707)));
		float ff = step( d1, 1.0 );
		co2 += ff;
		trap1 += ff*d1;

		//trap2
		trap2 = min( trap2, dot(z-t2c,z-t2c) );
    }

    // distance, d(c) = |Z|·log|Z|/|Z'|
	float d = sqrt( dot(z,z)/dot(dz,dz) )*log(dot(z,z));
	
	float c1 = pow( clamp( 2.00*d/zoo,    0.0, 1.0 ), 0.5 );
	float c2 = pow( clamp( 1.5*trap1/co2, 0.0, 1.0 ), 2.0 );
	float c3 = pow( clamp( 0.4*trap2, 0.0, 1.0 ), 0.25 );

	float3 col1 = 0.5 + 0.5*sin( 3.0 + 4.0*c2 + float3(0.0,0.5,1.0) );
	float3 col2 = 0.5 + 0.5*sin( 4.1 + 2.0*c3 + float3(1.0,0.5,0.0) );
	col += 2.0*sqrt(c1*col1*col2);
    }
	col /= float(AA*AA);
    
	fragColor = float4( col, 1.0 );
}

[shader("fragment")]
void fragmentMain(
	in float3 inColor,
	in float4 gl_FragCoord : SV_Position,
	out float4 outFragColor : SV_Target0
) 
{
	mainImage(outFragColor, gl_FragCoord.xy);
}

