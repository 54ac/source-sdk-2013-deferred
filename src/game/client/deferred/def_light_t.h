#ifndef DEF_LIGHT_H
#define DEF_LIGHT_H

#include "ehandle.h"

class CMeshBuilder;
class IDefCookie;

#define MAX_STATIC_SHADOW_ATLAS_SLOTS 128
class ConVar;
extern ConVar r_deferred_rt_shadowspot_static_res;

#define MAX_STATIC_POINT_SHADOW_SLOTS 8
#define STATIC_POINT_SHADOW_TILE_RES  1024
#define STATIC_POINT_SHADOW_RES       2048

struct def_light_presortdatax4_t
{
	fltx4		bounds_min_naive[3];
	fltx4		bounds_max_naive[3];
	i32x4		hasShadow;
	i32x4		hasVolumetrics;
	def_light_t* lights[4];
	uint16		count;				//this throws the alignment way out
};

struct def_light_t
{
	friend class CLightingManager;

	def_light_t( bool bWorld = false );
	virtual ~def_light_t();

	DECLARE_DATADESC();

	static def_light_t *AllocateFromKeyValues( KeyValues *pKV );
	KeyValues *AllocateAsKeyValues();
	void ApplyKeyValueProperties( KeyValues *pKV );

	bool bWorldLight;
	bool bWasShadowActive;
	bool bWasFullscreen;

	Vector pos;
	QAngle ang;

	Vector col_diffuse;
	Vector col_ambient;

	float flRadius;
	float flFalloffPower;

	uint8 iLighttype;
	uint8 iFlags;
	uint8 iCookieIndex;

	float flSpotCone_Inner;
	float flSpotCone_Outer;
	float flShadowFilterScale;

	uint16 iVisible_Dist;
	uint16 iVisible_Range;
	uint16 iShadow_Dist;
	uint16 iShadow_Range;

	uint16 iStyleSeed;
	float flStyle_Amount;
	float flStyle_Smooth;
	float flStyle_Random;
	float flStyle_Speed;

	int	iVolumeSamples;
	float flUberRoundness;
	float flUberWidth;
	float flUberHeight;
	float flUberWEdge;
	float flUberHEdge;
	float flUberShearX;
	float flUberShearY;
	float flUberCutOn;
	float flUberCutOff;
	float flUberNearEdge;
	float flUberFarEdge;

	void BuildBox( IMesh *pMesh );
	void BuildSphere( IMesh **pMesh );
	void BuildCone( IMesh *pMesh );
	void BuildConeFlipped( IMesh *pMesh );

	static void ShutdownSharedMeshes();

	FORCEINLINE bool IsWorldLight()
	{
		return bWorldLight;
	};

	FORCEINLINE bool IsDirty()
	{
		return ( iFlags & ( DEFLIGHT_DIRTY_XFORMS | DEFLIGHT_DIRTY_RENDERMESH ) ) != 0;
	};
	FORCEINLINE bool IsDirtyXForms()
	{
		return ( iFlags & DEFLIGHT_DIRTY_XFORMS ) != 0;
	};
	FORCEINLINE bool IsDirtyRenderMesh()
	{
		return ( iFlags & DEFLIGHT_DIRTY_RENDERMESH ) != 0;
	};

	FORCEINLINE void UnDirtyAll()
	{
		iFlags &= ~( DEFLIGHT_DIRTY_XFORMS | DEFLIGHT_DIRTY_RENDERMESH );
	};

	FORCEINLINE void MakeDirtyXForms()
	{
		iFlags |= DEFLIGHT_DIRTY_XFORMS;
	};
	FORCEINLINE void MakeDirtyRenderMesh()
	{
		iFlags |= DEFLIGHT_DIRTY_RENDERMESH;
	};
	FORCEINLINE void MakeDirtyAll()
	{
		iFlags |= DEFLIGHT_DIRTY_XFORMS | DEFLIGHT_DIRTY_RENDERMESH;
	};

	FORCEINLINE bool IsPoint()
	{
		return iLighttype == DEFLIGHTTYPE_POINT;
	};
	FORCEINLINE bool IsSpot()
	{
		return iLighttype == DEFLIGHTTYPE_SPOT;
	};

	FORCEINLINE bool HasShadow()
	{
		return ( iFlags & DEFLIGHT_SHADOW_ENABLED ) != 0;
	};
	FORCEINLINE bool HasCookie()
	{
		return ( iFlags & DEFLIGHT_COOKIE_ENABLED ) != 0;
	};
	FORCEINLINE bool HasVolumetrics()
	{
		return ( iFlags & DEFLIGHT_VOLUMETRICS_ENABLED ) != 0;
	};
	FORCEINLINE bool HasUberlight()
	{
		return ( iFlags & DEFLIGHT_UBERLIGHT_ENABLED ) != 0;
	};

	FORCEINLINE bool HasLightstyle()
	{
		return ( iFlags & DEFLIGHT_LIGHTSTYLE_ENABLED ) != 0;
	};

	FORCEINLINE bool ShouldRenderShadow()
	{
		return HasShadow() && flShadowFade < 1;
	};

	FORCEINLINE bool IsStatic() const
	{
		return m_bIsStatic || bWorldLight;
	};
	FORCEINLINE bool IsShadowDirty() const
	{
		return m_bShadowDirty;
	};
	FORCEINLINE void SetShadowDirty( bool bDirty )
	{
		m_bShadowDirty = bDirty;
	};
	FORCEINLINE bool HasValidAtlasSlot() const
	{
		return m_nCachedAtlasSlot >= 0;
	};

	bool IsCookieReady();
	ITexture *GetCookieForDraw( const int iTargetIndex = 0 );
	void ClearCookie();
	IDefCookie *CreateCookieInstance( const char *pszCookieName );
	void SetCookie( IDefCookie *pCookie );

	FORCEINLINE float GetFOV()
	{
		Assert( iLighttype == DEFLIGHTTYPE_SPOT );
		return flFOV;
	};

	FORCEINLINE void GetBoundsNaive( Vector &mins, Vector &maxs ) const
	{
		mins = bounds_min_naive;
		maxs = bounds_max_naive;
	};


private:

	def_light_t( const def_light_t &o );

	void DestroyMeshes();

	// *********************************************
	// * volatile data, not guaranteed to be valid *
	// *********************************************


	float flMaxDistSqr;

	void UpdateMatrix();
	float flFOV;
	VMatrix worldTransform;
	VMatrix spotMVPInv;
	VMatrix spotVPInv;
	VMatrix spotWorldToTex;

	void UpdateFrustum();
	Frustum_t spotFrustum;
	
	void UpdateXForms();
	Vector bounds_min, bounds_max;
	Vector bounds_min_naive, bounds_max_naive;

	void UpdateRenderMesh();
	IMesh *pMesh_World;

	void UpdateVolumetrics();
	IMesh *pMesh_Volumetrics;
	IMesh *pMesh_VolumPrepass;

#if DEBUG
	IMesh *pMesh_Debug;
	IMesh *pMesh_Debug_Volumetrics;
#endif

	static IMesh *pMeshUnitSphere;

	void UpdateCookieTexture();
	uint8 iOldCookieIndex;
	IDefCookie *pCookie;

	Vector backDir;
	Vector boundsCenter;

	float flDistance_ViewOrigin;
	float flShadowFade;

	float flLastRandomTime;
	float flLastRandomValue;

public:
	bool m_bIsStatic;
	bool m_bShadowDirty;
	int16 m_nCachedAtlasSlot;
	EHANDLE m_hIgnoreEntity;
};

struct def_light_temp_t : public def_light_t
{
	def_light_temp_t( float fLifeTime = 1.0f )
	{
		fLifeDuration = fLifeTime;
		fEndLifeTime = gpGlobals->curtime + fLifeTime;
		flStartRadius = 0.0f;
		vecStartColor.Init();
	}

	float fLifeDuration;
	float fEndLifeTime;
	float flStartRadius;
	Vector vecStartColor;
};

#endif