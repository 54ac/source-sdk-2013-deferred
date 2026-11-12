#ifndef COMMON_DITHER_FXC_H
#define COMMON_DITHER_FXC_H

// 4x4 Bayer dithering matrix
static const float g_flDitherMatrix4x4[4][4] = {
	{  1.0 / 17.0,  9.0 / 17.0,  3.0 / 17.0, 11.0 / 17.0 },
	{ 13.0 / 17.0,  5.0 / 17.0, 15.0 / 17.0,  7.0 / 17.0 },
	{  4.0 / 17.0, 12.0 / 17.0,  2.0 / 17.0, 10.0 / 17.0 },
	{ 16.0 / 17.0,  8.0 / 17.0, 14.0 / 17.0,  6.0 / 17.0 }
};

void ScreenDoorDitherClip( float flOpacity, float2 vScreenPos )
{
	if ( flOpacity < 0.999f )
	{
		int2 nCoord = (int2)fmod( abs( vScreenPos ), 4.0f );
		float flThreshold = g_flDitherMatrix4x4[nCoord.y][nCoord.x];
		clip( flOpacity - flThreshold );
	}
}

#endif // COMMON_DITHER_FXC_H
