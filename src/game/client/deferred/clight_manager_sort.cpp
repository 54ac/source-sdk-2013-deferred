#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_client_common.h"
#include "deferred/clight_manager.h"
#include "collisionutils.h"

#include "tier0/memdbgon.h"

void CLightingManager::AllocateSortDataBuffer()
{
	const int count = m_hDeferredLights.Count();
	if ( count == 0 )
		return;

	const int iSortBufferDataSize = count % 4 == 0
		                                ? count / 4
		                                : ( ( count - ( count % 4 ) ) / 4 ) + 1;

	if ( m_SortBufferDataSize >= iSortBufferDataSize )
		return;

	m_SortBufferDataSize = iSortBufferDataSize;
	m_pSortDataX4        = reinterpret_cast<def_light_presortdatax4_t*>(
		MemAlloc_ReallocAligned( m_pSortDataX4, iSortBufferDataSize * sizeof( def_light_presortdatax4_t ),
		                         sizeof( fltx4 ) * 4 ) );
}

void CLightingManager::BuildLightSortDataBuffer()
{
	const int iPartiallyFullSortDataElementCount = m_hRenderLights.Count() % 4;
	m_uiSortDataCount                            = ( m_hRenderLights.Count() - iPartiallyFullSortDataElementCount ) / 4;

	for ( uint i = 0, baseLightIdx = 0; i < m_uiSortDataCount; i++, baseLightIdx += 4 )
	{
		def_light_presortdatax4_t& sortData = m_pSortDataX4[i];

		sortData.count = 4;

		def_light_t* lights[4] =
		{
			m_hRenderLights[baseLightIdx],
			m_hRenderLights[baseLightIdx + 1],
			m_hRenderLights[baseLightIdx + 2],
			m_hRenderLights[baseLightIdx + 3]
		};

		sortData.lights[0] = lights[0];
		sortData.lights[1] = lights[1];
		sortData.lights[2] = lights[2];
		sortData.lights[3] = lights[3];

		float boundsMinX[4] =
		{
			lights[0]->bounds_min_naive.x,
			lights[1]->bounds_min_naive.x,
			lights[2]->bounds_min_naive.x,
			lights[3]->bounds_min_naive.x
		};
		sortData.bounds_min_naive[0] = LoadUnalignedSIMD( boundsMinX );

		float boundsMinY[4] =
		{
			lights[0]->bounds_min_naive.y,
			lights[1]->bounds_min_naive.y,
			lights[2]->bounds_min_naive.y,
			lights[3]->bounds_min_naive.y
		};
		sortData.bounds_min_naive[1] = LoadUnalignedSIMD( boundsMinY );

		float boundsMinZ[4] =
		{
			lights[0]->bounds_min_naive.z,
			lights[1]->bounds_min_naive.z,
			lights[2]->bounds_min_naive.z,
			lights[3]->bounds_min_naive.z
		};
		sortData.bounds_min_naive[2] = LoadUnalignedSIMD( boundsMinZ );

		float boundsMaxX[4] =
		{
			lights[0]->bounds_max_naive.x,
			lights[1]->bounds_max_naive.x,
			lights[2]->bounds_max_naive.x,
			lights[3]->bounds_max_naive.x
		};
		sortData.bounds_max_naive[0] = LoadUnalignedSIMD( boundsMaxX );

		float boundsMaxY[4] =
		{
			lights[0]->bounds_max_naive.y,
			lights[1]->bounds_max_naive.y,
			lights[2]->bounds_max_naive.y,
			lights[3]->bounds_max_naive.y
		};
		sortData.bounds_max_naive[1] = LoadUnalignedSIMD( boundsMaxY );

		float boundsMaxZ[4] =
		{
			lights[0]->bounds_max_naive.z,
			lights[1]->bounds_max_naive.z,
			lights[2]->bounds_max_naive.z,
			lights[3]->bounds_max_naive.z
		};
		sortData.bounds_max_naive[2] = LoadUnalignedSIMD( boundsMaxZ );

		int hasShadowX4[4] =
		{
			lights[0]->HasShadow() ? ~0 : 0,
			lights[1]->HasShadow() ? ~0 : 0,
			lights[2]->HasShadow() ? ~0 : 0,
			lights[3]->HasShadow() ? ~0 : 0
		};
		sortData.hasShadow = LoadUnalignedIntSIMD( hasShadowX4 );

		int hasVolumetricsX4[4] =
		{
			lights[0]->HasVolumetrics() ? ~0 : 0,
			lights[1]->HasVolumetrics() ? ~0 : 0,
			lights[2]->HasVolumetrics() ? ~0 : 0,
			lights[3]->HasVolumetrics() ? ~0 : 0
		};
		sortData.hasVolumetrics = LoadUnalignedIntSIMD( hasVolumetricsX4 );
	}

	if ( iPartiallyFullSortDataElementCount != 0 )
	{
		def_light_presortdatax4_t& sortData = m_pSortDataX4[m_uiSortDataCount];

		sortData.count = iPartiallyFullSortDataElementCount;

		float boundsMinX[4]       = { 0 };
		float boundsMinY[4]       = { 0 };
		float boundsMinZ[4]       = { 0 };
		float boundsMaxX[4]       = { 0 };
		float boundsMaxY[4]       = { 0 };
		float boundsMaxZ[4]       = { 0 };
		int   hasShadowX4[4]      = { 0 };
		int   hasVolumetricsX4[4] = { 0 };

		int iBaseLightIndex = m_uiSortDataCount * 4;

		for ( int i = 0; i < iPartiallyFullSortDataElementCount; i++ )
		{
			def_light_t* l = m_hRenderLights[iBaseLightIndex + i];

			sortData.lights[i] = l;

			boundsMinX[i] = l->bounds_min_naive.x;
			boundsMinY[i] = l->bounds_min_naive.y;
			boundsMinZ[i] = l->bounds_min_naive.z;

			boundsMaxX[i] = l->bounds_max_naive.x;
			boundsMaxY[i] = l->bounds_max_naive.y;
			boundsMaxZ[i] = l->bounds_max_naive.z;

			hasShadowX4[i] = l->HasShadow() ? ~0 : 0;

			hasVolumetricsX4[i] = l->HasVolumetrics() ? ~0 : 0;
		}

		sortData.bounds_min_naive[0] = LoadUnalignedSIMD( boundsMinX );
		sortData.bounds_min_naive[1] = LoadUnalignedSIMD( boundsMinY );
		sortData.bounds_min_naive[2] = LoadUnalignedSIMD( boundsMinZ );

		sortData.bounds_max_naive[0] = LoadUnalignedSIMD( boundsMaxX );
		sortData.bounds_max_naive[1] = LoadUnalignedSIMD( boundsMaxY );
		sortData.bounds_max_naive[2] = LoadUnalignedSIMD( boundsMaxZ );

		sortData.hasShadow = LoadUnalignedIntSIMD( hasShadowX4 );

		sortData.hasVolumetrics = LoadUnalignedIntSIMD( hasVolumetricsX4 );

		m_uiSortDataCount++;
	}
}

int __cdecl CLightingManager::LightDistanceCompare( def_light_t* const* a, def_light_t* const* b )
{
	const def_light_t* lA = *a;
	const def_light_t* lB = *b;

	const float distA = lA->flDistance_ViewOrigin * ( lA->bWasShadowActive ? 0.85f : 1.0f );
	const float distB = lB->flDistance_ViewOrigin * ( lB->bWasShadowActive ? 0.85f : 1.0f );

	if ( distA < distB ) return -1;
	if ( distA > distB ) return 1;
	return 0;
}

void CLightingManager::SortLights()
{
#if DEBUG
	for ( int i = 0; i < LSORT_COUNT; i++ )
		Assert( m_hPreSortedLights[ i ].Count() == 0 );
#endif

	m_bDrawVolumetrics = false;

	const float zNear = m_flzNear + 2;

	const Vector vecBloat( zNear, zNear, zNear );

	const Vector camMins( m_vecViewOrigin - vecBloat );
	const Vector camMaxs( m_vecViewOrigin + vecBloat );

	const fltx4 camMinsX = ReplicateX4( camMins.x );
	const fltx4 camMinsY = ReplicateX4( camMins.y );
	const fltx4 camMinsZ = ReplicateX4( camMins.z );
	const fltx4 camMaxsX = ReplicateX4( camMaxs.x );
	const fltx4 camMaxsY = ReplicateX4( camMaxs.y );
	const fltx4 camMaxsZ = ReplicateX4( camMaxs.z );

	for ( uint i = 0; i < m_uiSortDataCount; i++ )
	{
		def_light_presortdatax4_t& s = m_pSortDataX4[i];

		fltx4 boxIntersects = AndSIMD(
			AndSIMD(
				AndSIMD( CmpGeSIMD( camMaxsX, s.bounds_min_naive[0] ), CmpLeSIMD( camMinsX, s.bounds_max_naive[0] ) ),
				AndSIMD( CmpGeSIMD( camMaxsY, s.bounds_min_naive[1] ), CmpLeSIMD( camMinsY, s.bounds_max_naive[1] ) )
			),
			AndSIMD( CmpGeSIMD( camMaxsZ, s.bounds_min_naive[2] ), CmpLeSIMD( camMinsZ, s.bounds_max_naive[2] ) )
		);

		for ( uint j = 0; j < s.count; j++ )
		{
			def_light_t *pLight = s.lights[j];

			const bool bHasVolumetrics = ( pLight->HasVolumetrics() && pLight->HasShadow() && ( r_deferred_volumetric_intensity.GetFloat() > 0.0f ) );
			if ( bHasVolumetrics )
			{
				m_bDrawVolumetrics = true;
			}

			if ( pLight->IsSpot() )
			{
				bool bNeedsFullscreen = false;
				if ( ( SubInt( boxIntersects, j ) & 0x80000000 ) != 0 )
				{
					bNeedsFullscreen = !R_CullBox( camMins, camMaxs, pLight->spotFrustum );
				}
				pLight->bWasFullscreen = bNeedsFullscreen;
				m_hPreSortedLights[ bNeedsFullscreen ? LSORT_SPOT_FULLSCREEN : LSORT_SPOT_WORLD ].AddToTail( pLight );
			}
			else
			{
				const float flPointDistSqr = ( pLight->pos - m_vecViewOrigin ).LengthSqr();
				const float flBaseThreshold = pLight->flRadius * 1.05f + zNear + 48.0f;
				const float flHysteresis = ( pLight->bWasFullscreen ? 32.0f : 0.0f );
				const float flPointNearDist = flBaseThreshold + flHysteresis;
				const bool bNeedsFullscreen = ( flPointDistSqr < flPointNearDist * flPointNearDist );
				pLight->bWasFullscreen = bNeedsFullscreen;
				m_hPreSortedLights[ bNeedsFullscreen ? LSORT_POINT_FULLSCREEN : LSORT_POINT_WORLD ].AddToTail( pLight );
			}
		}
	}

	m_hPreSortedLights[LSORT_POINT_FULLSCREEN].Sort( LightDistanceCompare );
	m_hPreSortedLights[LSORT_SPOT_FULLSCREEN].Sort( LightDistanceCompare );
}
