#include "cbase.h"
#include "cvolume_culler.h"

CSMCullStats_t g_CSMCullStats = { { 0, 0 }, { 0, 0 } };

CVolumeCuller::CVolumeCuller()
{
	Clear();
}

void CVolumeCuller::Clear()
{
	m_nNumPlanes = 0;
	m_nNumPacks = 0;
	ResetStats();
}

int CVolumeCuller::InitFromFrustum( const Vector *pCorners8, const Vector &vSunDir, float flMaxCasterDist )
{
	m_nNumPlanes = 0;
	m_nNumPacks = 0;
	if ( !pCorners8 )
		return 0;

	Vector vCenter( 0, 0, 0 );
	for ( int i = 0; i < 8; ++i )
	{
		vCenter += pCorners8[i];
	}
	vCenter *= ( 1.0f / 8.0f );

	// Corner layout from ComputeFrustumCorners:
	// 0: Near BL (-X, -Y)
	// 1: Near BR (+X, -Y)
	// 2: Near TL (-X, +Y)
	// 3: Near TR (+X, +Y)
	// 4: Far BL  (-X, -Y)
	// 5: Far BR  (+X, -Y)
	// 6: Far TL  (-X, +Y)
	// 7: Far TR  (+X, +Y)

	static const int s_QuadVerts[6][4] = {
		{ 0, 4, 6, 2 }, // 0: Left   (-X)
		{ 1, 3, 7, 5 }, // 1: Right  (+X)
		{ 0, 1, 5, 4 }, // 2: Bottom (-Y)
		{ 2, 6, 7, 3 }, // 3: Top    (+Y)
		{ 0, 2, 3, 1 }, // 4: Near
		{ 4, 5, 7, 6 }  // 5: Far
	};

	bool bFacing[6];
	VPlane frustumPlanes[6];

	for ( int q = 0; q < 6; ++q )
	{
		const Vector &va = pCorners8[s_QuadVerts[q][0]];
		const Vector &vb = pCorners8[s_QuadVerts[q][1]];
		const Vector &vc = pCorners8[s_QuadVerts[q][2]];

		Vector v1 = vb - va;
		Vector v2 = vc - va;
		Vector vNorm = CrossProduct( v1, v2 );
		if ( vNorm.LengthSqr() < 1e-6f )
		{
			bFacing[q] = false;
			continue;
		}
		vNorm.NormalizeInPlace();

		float flDist = DotProduct( vNorm, va );
		if ( DotProduct( vNorm, vCenter ) - flDist < 0.0f )
		{
			vNorm = -vNorm;
			flDist = -flDist;
		}

		frustumPlanes[q].Init( vNorm, flDist );

		const bool bFacesLight = ( DotProduct( vNorm, vSunDir ) < 0.0f );
		bFacing[q] = bFacesLight;

		if ( bFacesLight && m_nNumPlanes < MAX_CULLING_PLANES )
		{
			m_Planes[m_nNumPlanes++] = frustumPlanes[q];
		}
	}

	Vector vDirToLight = -vSunDir;
	Vector vExtrudedVerts[16];
	for ( int i = 0; i < 8; ++i )
	{
		vExtrudedVerts[i] = pCorners8[i];
		vExtrudedVerts[i + 8] = pCorners8[i] + vDirToLight * flMaxCasterDist;
	}

	VPlane topCapPlane;
	topCapPlane.m_Normal = vSunDir;
	float flMinDot = DotProduct( vExtrudedVerts[0], vSunDir );
	for ( int i = 1; i < 16; ++i )
	{
		float flDot = DotProduct( vExtrudedVerts[i], vSunDir );
		if ( flDot < flMinDot )
			flMinDot = flDot;
	}
	topCapPlane.m_Dist = flMinDot - 8.0f;

	if ( m_nNumPlanes < MAX_CULLING_PLANES )
	{
		m_Planes[m_nNumPlanes++] = topCapPlane;
	}

	struct FrustumEdge_t
	{
		int v0, v1;
		int q0, q1;
	};
	static const FrustumEdge_t s_Edges[12] = {
		{ 0, 2, 0, 4 }, // Near-Left
		{ 1, 3, 1, 4 }, // Near-Right
		{ 0, 1, 2, 4 }, // Near-Bottom
		{ 2, 3, 3, 4 }, // Near-Top
		{ 4, 6, 0, 5 }, // Far-Left
		{ 5, 7, 1, 5 }, // Far-Right
		{ 4, 5, 2, 5 }, // Far-Bottom
		{ 6, 7, 3, 5 }, // Far-Top
		{ 0, 4, 0, 2 }, // Side-Bottom-Left
		{ 1, 5, 1, 2 }, // Side-Bottom-Right
		{ 2, 6, 0, 3 }, // Side-Top-Left
		{ 3, 7, 1, 3 }  // Side-Top-Right
	};

	for ( int e = 0; e < 12; ++e )
	{
		if ( bFacing[s_Edges[e].q0] == bFacing[s_Edges[e].q1] )
			continue;

		const Vector &va = pCorners8[s_Edges[e].v0];
		const Vector &vb = pCorners8[s_Edges[e].v1];

		Vector vEdge = va - vb;
		vEdge.NormalizeInPlace();

		Vector vNorm = CrossProduct( vEdge, vSunDir );
		if ( vNorm.LengthSqr() < 1e-6f )
			continue;
		vNorm.NormalizeInPlace();

		float flDist = DotProduct( vNorm, va );
		if ( DotProduct( vNorm, vCenter ) - flDist < 0.0f )
		{
			vNorm = -vNorm;
			flDist = -flDist;
		}

		if ( m_nNumPlanes < MAX_CULLING_PLANES )
		{
			m_Planes[m_nNumPlanes++] = VPlane( vNorm, flDist );
		}
	}

	PackPlanesSIMD();
	return m_nNumPlanes;
}

void CVolumeCuller::PackPlanesSIMD()
{
	m_nNumPacks = ( m_nNumPlanes + 3 ) / 4;

	for ( int p = 0; p < m_nNumPacks; ++p )
	{
		for ( int i = 0; i < 4; ++i )
		{
			int planeIdx = p * 4 + i;
			if ( planeIdx < m_nNumPlanes )
			{
				const VPlane &plane = m_Planes[planeIdx];
				m_Packs[p].m_NormalX[i] = plane.m_Normal.x;
				m_Packs[p].m_NormalY[i] = plane.m_Normal.y;
				m_Packs[p].m_NormalZ[i] = plane.m_Normal.z;
				m_Packs[p].m_Dist[i] = plane.m_Dist;
				m_Packs[p].m_AbsNormalX[i] = fabsf( plane.m_Normal.x );
				m_Packs[p].m_AbsNormalY[i] = fabsf( plane.m_Normal.y );
				m_Packs[p].m_AbsNormalZ[i] = fabsf( plane.m_Normal.z );
			}
			else
			{
				m_Packs[p].m_NormalX[i] = 0.0f;
				m_Packs[p].m_NormalY[i] = 0.0f;
				m_Packs[p].m_NormalZ[i] = 0.0f;
				m_Packs[p].m_Dist[i] = -1e9f;
				m_Packs[p].m_AbsNormalX[i] = 0.0f;
				m_Packs[p].m_AbsNormalY[i] = 0.0f;
				m_Packs[p].m_AbsNormalZ[i] = 0.0f;
			}
		}
	}
}

bool CVolumeCuller::CheckBox( const Vector &mins, const Vector &maxs ) const
{
	m_Stats.nTotalTested++;

	if ( m_nNumPlanes == 0 )
		return true;

	Vector center = ( maxs + mins ) * 0.5f;
	Vector extents = ( maxs - mins ) * 0.5f;

	fltx4 cX = ReplicateX4( center.x );
	fltx4 cY = ReplicateX4( center.y );
	fltx4 cZ = ReplicateX4( center.z );

	fltx4 eX = ReplicateX4( extents.x );
	fltx4 eY = ReplicateX4( extents.y );
	fltx4 eZ = ReplicateX4( extents.z );

	for ( int p = 0; p < m_nNumPacks; ++p )
	{
		const PlanePack4_t &pack = m_Packs[p];

		fltx4 normX = LoadUnalignedSIMD( pack.m_NormalX );
		fltx4 normY = LoadUnalignedSIMD( pack.m_NormalY );
		fltx4 normZ = LoadUnalignedSIMD( pack.m_NormalZ );
		fltx4 dist  = LoadUnalignedSIMD( pack.m_Dist );
		fltx4 absX  = LoadUnalignedSIMD( pack.m_AbsNormalX );
		fltx4 absY  = LoadUnalignedSIMD( pack.m_AbsNormalY );
		fltx4 absZ  = LoadUnalignedSIMD( pack.m_AbsNormalZ );

		fltx4 dot = AddSIMD( MulSIMD( normX, cX ),
		            AddSIMD( MulSIMD( normY, cY ),
		                     MulSIMD( normZ, cZ ) ) );
		fltx4 centerDist = SubSIMD( dot, dist );

		fltx4 radius = AddSIMD( MulSIMD( absX, eX ),
		               AddSIMD( MulSIMD( absY, eY ),
		                        MulSIMD( absZ, eZ ) ) );

		fltx4 maxDist = AddSIMD( centerDist, radius );

		if ( TestSignSIMD( CmpLtSIMD( maxDist, Four_Zeros ) ) != 0 )
		{
			m_Stats.nCulled++;
			return false;
		}
	}

	return true;
}
