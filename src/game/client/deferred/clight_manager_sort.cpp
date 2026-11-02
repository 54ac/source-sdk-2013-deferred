#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_client_common.h"
#include "deferred/clight_manager.h"
#include "collisionutils.h"

#include "tier0/memdbgon.h"

#if DEFCFG_USE_SSE
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
#endif

inline fltx4 IsPointInBoundsX4( const Vector& point, fltx4 boundsMin[3], fltx4 boundsMax[3] )
{
	const fltx4& pointX = ReplicateX4( point.x );
	const fltx4& pointY = ReplicateX4( point.y );
	const fltx4& pointZ = ReplicateX4( point.z );

	return AndSIMD
	(
		AndSIMD
		(
			AndSIMD
			(
				CmpGeSIMD( pointX, boundsMin[0] ),
				CmpLeSIMD( pointX, boundsMax[0] )
			),
			AndSIMD
			(
				CmpGeSIMD( pointY, boundsMin[1] ),
				CmpLeSIMD( pointY, boundsMax[1] )
			)
		),
		AndSIMD
		(
			CmpGeSIMD( pointZ, boundsMin[2] ),
			CmpLeSIMD( pointZ, boundsMax[2] )
		)
	);
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

#if DEFCFG_USE_SSE
#if !DEFCFG_EXTRA_SORT
	const fltx4 camMinsX = ReplicateX4( camMins.x );
	const fltx4 camMinsY = ReplicateX4( camMins.y );
	const fltx4 camMinsZ = ReplicateX4( camMins.z );
	const fltx4 camMaxsX = ReplicateX4( camMaxs.x );
	const fltx4 camMaxsY = ReplicateX4( camMaxs.y );
	const fltx4 camMaxsZ = ReplicateX4( camMaxs.z );
#endif

	for ( uint i = 0; i < m_uiSortDataCount; i++ )
	{
		def_light_presortdatax4_t& s = m_pSortDataX4[i];

#if DEFCFG_EXTRA_SORT
		const fltx4& zNearX4 = ReplicateX4( zNear );
		fltx4 adjustedMins[3] = {
			SubSIMD( s.bounds_min_naive[0], zNearX4 ),
			SubSIMD( s.bounds_min_naive[1], zNearX4 ),
			SubSIMD( s.bounds_min_naive[2], zNearX4 )
		};

		fltx4 adjustedMaxs[3] = {
			AddSIMD( s.bounds_max_naive[0], zNearX4 ),
			AddSIMD( s.bounds_max_naive[1], zNearX4 ),
			AddSIMD( s.bounds_max_naive[2], zNearX4 )
		};

		fltx4 needsFullscreen = IsPointInBoundsX4( m_vecViewOrigin,
		                                           adjustedMins, adjustedMaxs );

		//Jack: this is terrible I know
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
				if ( IS_NAN( SubFloat( needsFullscreen, j ) ) )
				{
					bNeedsFullscreen = true;
				}
				else if ( !bNeedsFullscreen && ( DotProduct( m_vecForward, pLight->pos - m_vecViewOrigin ) < 32.0f ) )
				{
					bNeedsFullscreen = true;
				}
				else if ( !bNeedsFullscreen && ( pLight->flDistance_ViewOrigin < ( pLight->flRadius + zNear + 32.0f ) ) )
				{
					bNeedsFullscreen = true;
				}

				if ( bNeedsFullscreen )
				{
					m_hPreSortedLights[LSORT_SPOT_FULLSCREEN].AddToTail( pLight );
				}
				else
				{
					m_hPreSortedLights[LSORT_SPOT_WORLD].AddToTail( pLight );
				}
			}
			else
			{
				bool bNeedsFullscreen = IS_NAN( SubFloat( needsFullscreen, j ) );
				if ( ( s.lights[j]->HasShadow() && !s.lights[j]->IsStatic() ) || ( !bNeedsFullscreen && ( s.lights[j]->flDistance_ViewOrigin < ( s.lights[j]->flRadius * 1.05f + zNear + 48.0f ) ) ) )
				{
					bNeedsFullscreen = true;
				}
				s.lights[j]->bWasFullscreen = bNeedsFullscreen;

				if ( bNeedsFullscreen )
				{
					m_hPreSortedLights[LSORT_POINT_FULLSCREEN].AddToTail( s.lights[j] );
				}
				else
				{
					m_hPreSortedLights[LSORT_POINT_WORLD].AddToTail( s.lights[j] );
				}
			}
		}
#else
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
#endif
	}
#else
	FOR_EACH_VEC_FAST( def_light_t*, m_hRenderLights, l )
	{
		const bool bHasVolumetrics = ( l->HasVolumetrics() && l->HasShadow() && ( r_deferred_volumetric_intensity.GetFloat() > 0.0f ) );
		if ( bHasVolumetrics )
		{
			m_bDrawVolumetrics = true;
		}

#if DEFCFG_EXTRA_SORT
		bool bNeedsFullscreen = IsPointInBounds( m_vecViewOrigin,
			l->bounds_min_naive - vecBloat,
			l->bounds_max_naive + vecBloat );

		if ( !bNeedsFullscreen && l->IsSpot() && ( DotProduct( m_vecForward, l->pos - m_vecViewOrigin ) < 32.0f ) )
		{
			bNeedsFullscreen = true;
		}

		if ( !bNeedsFullscreen && ( l->flDistance_ViewOrigin < ( l->flRadius * 1.05f + zNear + 48.0f ) ) )
		{
			bNeedsFullscreen = true;
		}
		l->bWasFullscreen = bNeedsFullscreen;

		Assert( l->iLighttype * 2 + 1 < LSORT_COUNT );
		m_hPreSortedLights[ l->iLighttype * 2 + (int)bNeedsFullscreen ].AddToTail( l );
#else
		if ( l->IsSpot() )
		{
			const bool bNeedsFullscreen = !R_CullBox( camMins, camMaxs, l->spotFrustum );
			m_hPreSortedLights[ bNeedsFullscreen ? LSORT_SPOT_FULLSCREEN : LSORT_SPOT_WORLD ].AddToTail( l );
		}
		else
		{
			const float flPointDistSqr = ( l->pos - m_vecViewOrigin ).LengthSqr();
			const float flBaseThreshold = l->flRadius * 1.05f + zNear + 48.0f;
			const float flHysteresis = ( l->bWasFullscreen ? 32.0f : 0.0f );
			const float flPointNearDist = flBaseThreshold + flHysteresis;
			const bool bNeedsFullscreen = ( flPointDistSqr < flPointNearDist * flPointNearDist );
			l->bWasFullscreen = bNeedsFullscreen;
			m_hPreSortedLights[ bNeedsFullscreen ? LSORT_POINT_FULLSCREEN : LSORT_POINT_WORLD ].AddToTail( l );
		}
#endif
	}
	FOR_EACH_VEC_FAST_END
#endif

#if DEFCFG_EXTRA_SORT
	static CUtlVector<def_light_t*> hBatchFullscreen;

	for ( int i = 0; i < 2; i++ )
	{
		FOR_EACH_VEC_FAST( def_light_t*, m_hPreSortedLights[ i * 2 ], l )
		{
			const bool  bSpot        = l->IsSpot();
			const float flSizeFactor = bSpot ? 1.5f : 2.25f;

			const Vector& viewVec = l->boundsCenter - m_vecViewOrigin;
			const float   flDot   = DotProduct( m_vecForward, viewVec.Normalized() );

			float flCoverageFactor = 0.0f;
			if ( l->flDistance_ViewOrigin < ( l->flRadius * 1.35f + zNear + 32.0f ) )
			{
				flCoverageFactor = 2.0f;
			}
			else if ( flDot > 0.0f )
			{
				flCoverageFactor = flSizeFactor * l->flRadius / l->flDistance_ViewOrigin * flDot;
			}

			if ( flCoverageFactor > 1 )
				hBatchFullscreen.AddToTail( l );
		}
		FOR_EACH_VEC_FAST_END

		if ( hBatchFullscreen.Count() > 0 )
		{
			FOR_EACH_VEC_FAST( def_light_t*, hBatchFullscreen, l )
			{
#if DEBUG
				Assert( m_hPreSortedLights[ i * 2 ].FindAndRemove( l ) );
#else
				m_hPreSortedLights[i * 2].FindAndRemove( l );
#endif
				m_hPreSortedLights[i * 2 + 1].AddToTail( l );
			}
			FOR_EACH_VEC_FAST_END
		}

		hBatchFullscreen.RemoveAll();
	}
#endif

	m_hPreSortedLights[LSORT_POINT_FULLSCREEN].Sort( LightDistanceCompare );
	m_hPreSortedLights[LSORT_SPOT_FULLSCREEN].Sort( LightDistanceCompare );
}
