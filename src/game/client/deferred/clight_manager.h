#ifndef C_LIGHT_MANAGER_H
#define C_LIGHT_MANAGER_H

#include "deferred/deferred_shared_common.h"
#include "deferred/def_light_t.h"
#include "cliententitylist.h"
#include "const.h"

class CViewSetup;
class CDeferredViewRender;
class C_BaseEntity;
class ConVar;
extern ConVar r_deferred_shadow_cache;
#if DEFCFG_USE_SSE
struct def_light_presortdatax4_t;
#endif

class CLightingManager : public CAutoGameSystemPerFrame, public IClientEntityListener
{
	typedef CAutoGameSystemPerFrame BaseClass;
public:

	CLightingManager();
	~CLightingManager();

	virtual bool Init();
	virtual void Shutdown();

	virtual void LevelInitPostEntity();
	virtual void LevelShutdownPostEntity();

	virtual void Update( float ft );

	void SetRenderWorldLights( bool bRender );

	void LightSetup( const CViewSetup &setup );
	void LightTearDown();
	void SetRenderConstants( const VMatrix &ScreenToWorld,
		const CViewSetup &setup );

	void OnCookieStringReceived( const char *pszString, const int &index );
	void OnMaterialReload();

	void AddLight( def_light_t *l );
	bool RemoveLight( def_light_t *l );
	bool IsLightRendered( def_light_t *l ) const;

	int CountTempLights() const;
	void AddTempLight( def_light_temp_t *l );
	void UpdateTemplights(float deltaTime);

	// Unified dynamic light helpers
	static Vector SanitizeLightOrigin( const Vector &vecOrigin, const Vector *pFallback = NULL, float flMinClearance = 14.0f );

	def_light_t *AllocDynamicPointLight( const Vector &vecOrigin, const Vector &vecColor, float flRadius,
		int iFlags = DEFLIGHT_SHADOW_ENABLED, float flVisibleDist = 16384.0f, const Vector *pFallbackOrigin = NULL,
		C_BaseEntity *pIgnoreEntity = NULL );

	def_light_t *AllocDynamicSpotLight( const Vector &vecOrigin, const QAngle &vecAngles, const Vector &vecColor,
		float flRadius, float flInnerConeDeg, float flOuterConeDeg, int iFlags = DEFLIGHT_SHADOW_ENABLED,
		float flVisibleDist = 16384.0f, const Vector *pFallbackOrigin = NULL,
		C_BaseEntity *pIgnoreEntity = NULL );

	void UpdateDynamicLight( def_light_t *pLight, const Vector &vecOrigin, const Vector &vecColor, float flRadius,
		const QAngle *pAngles = NULL, bool bSanitizeOrigin = true, const Vector *pFallbackOrigin = NULL,
		C_BaseEntity *pIgnoreEntity = NULL );

	bool EnsureDynamicPointLight( def_light_t *&pLight, const Vector &vecOrigin, const Vector &vecColor, float flRadius,
		int iFlags = DEFLIGHT_SHADOW_ENABLED, float flVisibleDist = 16384.0f, const Vector *pFallbackOrigin = NULL,
		C_BaseEntity *pIgnoreEntity = NULL );

	bool EnsureDynamicSpotLight( def_light_t *&pLight, const Vector &vecOrigin, const QAngle &vecAngles, const Vector &vecColor,
		float flRadius, float flInnerConeDeg, float flOuterConeDeg, int iFlags = DEFLIGHT_SHADOW_ENABLED,
		float flVisibleDist = 16384.0f, const Vector *pFallbackOrigin = NULL,
		C_BaseEntity *pIgnoreEntity = NULL );

	void FreeDynamicLight( def_light_t *&pLight, float flDecayTime = 0.0f );

#if DEFCFG_USE_SSE
	void AllocateSortDataBuffer();
	void BuildLightSortDataBuffer();
#endif

	int AssignStaticAtlasSlot( def_light_t *l );
	void FreeStaticAtlasSlot( def_light_t *l );
	void ResetStaticAtlasSlots();
	void CheckDynamicEntityIntersections();
	virtual void OnEntityCreated( C_BaseEntity *pEntity ) OVERRIDE;
	virtual void OnEntityDeleted( C_BaseEntity *pEntity ) OVERRIDE;
	void OnEntityRemoved( C_BaseEntity *pEnt );
	void DirtyStaticLightsInBox( const Vector &vecCenter, float flRadius );
	bool DoesLightIntersectBox( def_light_t *l, const Vector &vecCenter, float flPropRadius, const Vector &mins, const Vector &maxs );
	bool DoesLightIntersectEntity( def_light_t *l, C_BaseEntity *pEnt );
	int GetStaticAtlasSlotCount() const;
	int GetStaticPointAtlasSlotCount() const;
	void ReevaluateStaticAtlasSlots();

	void ClearTmpLists();

	// update volatile data - internal xforms, final light col bleh
	void PrepareLights();

	// initialize the render list
	void CullLights();

	// determine which lights to draw fullscreen or per world projection
	void SortLights();

	// draw all lights set up for rendering
	void RenderLights( const CViewSetup &view, CDeferredViewRender *pCaller );

	// add volumes to scene after composition
	void RenderVolumetrics( const CViewSetup &view );

	// debugging crap
	void DoSceneDebug();
	void DebugLights_Draw_Boundingboxes();
#if DEBUG
	void DebugLights_Draw_DebugMeshes();
#endif

private:

	bool m_bDrawWorldLights;

	enum
	{
#if DEFCFG_EXTRA_SORT
		LSORT_POINT_WORLD_SIMPLE = 0,
		LSORT_POINT_WORLD_ADVANCED,
		LSORT_POINT_FULLSCREEN_SIMPLE,
		LSORT_POINT_FULLSCREEN_ADVANCED,
		LSORT_SPOT_WORLD_SIMPLE,
		LSORT_SPOT_WORLD_ADVANCED,
		LSORT_SPOT_FULLSCREEN_SIMPLE,
		LSORT_SPOT_FULLSCREEN_ADVANCED,
#else
		LSORT_POINT_WORLD = 0,
		LSORT_POINT_FULLSCREEN,
		LSORT_SPOT_WORLD,
		LSORT_SPOT_FULLSCREEN,
#endif

		LSORT_COUNT,
	};

#if DEFCFG_USE_SSE
	def_light_presortdatax4_t* m_pSortDataX4;
	int m_SortBufferDataSize;
	unsigned int m_uiSortDataCount;
	bool m_bSortDataNeedsRealloc;
#endif

	CUtlVector< def_light_t* > m_hDeferredLights;
	CUtlVector< def_light_temp_t* > m_hDeferredTempLights;

	CUtlVector< def_light_t* > m_hRenderLights;
	CUtlVector< def_light_t* > m_hPreSortedLights[ LSORT_COUNT ];

	VMatrix m_matScreenToWorld;
	Vector m_vecViewOrigin;
	Vector m_vecForward;
	float m_flzNear;
	bool m_bDrawVolumetrics;
	struct CachedPropState_t
	{
		Vector vecOrigin;
		Vector vecCenter;
		QAngle angAngles;
		float  flRadius;
		bool   bActive;
	};
	CachedPropState_t *m_pCachedPropStates;

	static int __cdecl LightDistanceCompare( def_light_t* const* a, def_light_t* const* b );
	FORCEINLINE float DoLightStyle( def_light_t *l );
	FORCEINLINE int WriteLight( def_light_t *l, float *pfl4, int bShadow = -1, int bCookie = -1 );
	FORCEINLINE void DrawVolumePrepass( bool bDoModelTransform, const CViewSetup &view, def_light_t *l );

#if DEBUG
	bool m_bVolatileLists;
#endif
};

extern CLightingManager *GetLightingManager();

#endif
