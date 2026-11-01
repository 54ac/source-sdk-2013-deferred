#ifndef CVOLUME_CULLER_H
#define CVOLUME_CULLER_H

#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include "mathlib/vplane.h"
#include "mathlib/ssemath.h"

struct CSMCullStats_t
{
	int nTested[2];
	int nCulled[2];
};
extern CSMCullStats_t g_CSMCullStats;

class CVolumeCuller
{
public:
	enum {
		MAX_CULLING_PLANES = 12
	};

	struct Stats_t
	{
		int nTotalTested;
		int nCulled;
	};

	CVolumeCuller();

	void Clear();

	// Initializes the 11-plane convex culling volume from:
	// - pCorners8: 8 world-space corner points of the receiver view frustum
	// - vSunDir: unit vector pointing in the direction sunlight travels (ray vector)
	// - flMaxCasterDist: maximum shadow caster extrusion distance along -vSunDir
	int InitFromFrustum( const Vector *pCorners8, const Vector &vSunDir, float flMaxCasterDist );

	bool CheckBox( const Vector &mins, const Vector &maxs ) const;

	int GetNumPlanes() const { return m_nNumPlanes; }
	const VPlane &GetPlane( int i ) const { return m_Planes[i]; }

	void ResetStats() { m_Stats.nTotalTested = 0; m_Stats.nCulled = 0; }
	const Stats_t &GetStats() const { return m_Stats; }

private:
	void PackPlanesSIMD();

	int m_nNumPlanes;
	VPlane m_Planes[MAX_CULLING_PLANES];

	struct PlanePack4_t
	{
		float m_NormalX[4];
		float m_NormalY[4];
		float m_NormalZ[4];
		float m_Dist[4];
		float m_AbsNormalX[4];
		float m_AbsNormalY[4];
		float m_AbsNormalZ[4];
	};

	int m_nNumPacks;
	PlanePack4_t m_Packs[3];

	mutable Stats_t m_Stats;
};

class CCascadeCuller
{
public:
	CCascadeCuller() : m_bHasRefl( false ) {}

	void Clear()
	{
		m_MainCuller.Clear();
		m_ReflCuller.Clear();
		m_bHasRefl = false;
	}

	void Init( const Vector *pCorners8, const Vector &vSunDir, float flMaxCasterDist, const Vector *pReflCorners8 = NULL )
	{
		m_MainCuller.InitFromFrustum( pCorners8, vSunDir, flMaxCasterDist );
		if ( pReflCorners8 )
		{
			m_ReflCuller.InitFromFrustum( pReflCorners8, vSunDir, flMaxCasterDist );
			m_bHasRefl = true;
		}
		else
		{
			m_bHasRefl = false;
		}
	}

	inline bool CheckBox( const Vector &mins, const Vector &maxs ) const
	{
		if ( !m_bHasRefl )
			return m_MainCuller.CheckBox( mins, maxs );

		if ( m_MainCuller.CheckBox( mins, maxs ) )
			return true;

		return m_ReflCuller.CheckBox( mins, maxs );
	}

	void ResetStats()
	{
		m_MainCuller.ResetStats();
		m_ReflCuller.ResetStats();
	}

	CVolumeCuller::Stats_t GetStats() const
	{
		CVolumeCuller::Stats_t total = m_MainCuller.GetStats();
		if ( m_bHasRefl )
		{
			CVolumeCuller::Stats_t refl = m_ReflCuller.GetStats();
			total.nTotalTested += refl.nTotalTested;
			total.nCulled += refl.nCulled;
		}
		return total;
	}

	const CVolumeCuller &GetMainCuller() const { return m_MainCuller; }

private:
	CVolumeCuller m_MainCuller;
	CVolumeCuller m_ReflCuller;
	bool m_bHasRefl;
};

#endif // CVOLUME_CULLER_H
