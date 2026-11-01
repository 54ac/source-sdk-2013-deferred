#include "cbase.h"
#include "c_physicsprop.h"
#include "c_gib.h"
#include "c_baseviewmodel.h"
#include "c_prop_vehicle.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/vgui/vgui_deferred.h"
#include "deferred/deferred_rt.h"
#include "deferred/cvolume_culler.h"
#include "physics_shared.h"

static void OnDeferredShadowCacheChanged( IConVar *var, const char *pOldValue, float flOldValue );
ConVar r_deferred_shadow_cache( "r_deferred_shadow_cache", "1", FCVAR_ARCHIVE, "Static shadow map caching for stationary world lights", OnDeferredShadowCacheChanged );
#include "collisionutils.h"
#include "materialsystem/imaterialvar.h"

#include "viewrender.h"
#include "view_shared.h"
#include "engine/ivdebugoverlay.h"
#include "tier0/fasttimer.h"
#include "tier1/callqueue.h"

#include "tier0/memdbgon.h"


static CLightingManager __g_lightingMan;
static bool s_bAtlasSlotUsed[ MAX_STATIC_SHADOW_ATLAS_SLOTS ];
static bool s_bAtlasSlotUsed_Point[ MAX_STATIC_POINT_SHADOW_SLOTS ];

CLightingManager* GetLightingManager()
{
	return &__g_lightingMan;
}

static void LightingResourceRelease()
{
	GetLightingManager()->OnMaterialReload();
}

CLightingManager::CLightingManager() : BaseClass( "LightingManagerSystem" )
{
#if DEBUG
	m_bVolatileLists = false;
#endif

	m_bDrawWorldLights = true;

	MatrixSetIdentity( m_matScreenToWorld );

	m_vecViewOrigin.Init();
	m_vecForward.Init();
	m_flzNear          = 0;
	m_pCachedPropStates = NULL;
	Q_memset( s_bAtlasSlotUsed, 0, sizeof( s_bAtlasSlotUsed ) );
	Q_memset( s_bAtlasSlotUsed_Point, 0, sizeof( s_bAtlasSlotUsed_Point ) );
	m_bDrawVolumetrics = false;
#if DEFCFG_USE_SSE
	m_pSortDataX4     = NULL;
	m_uiSortDataCount = 0;
#endif
}

CLightingManager::~CLightingManager() {}

bool CLightingManager::Init()
{
	materials->AddReleaseFunc( LightingResourceRelease );
	if ( cl_entitylist )
	{
		cl_entitylist->AddListenerEntity( this );
	}
	m_pCachedPropStates = new CachedPropState_t[ NUM_ENT_ENTRIES ];
	Q_memset( m_pCachedPropStates, 0, sizeof( CachedPropState_t ) * NUM_ENT_ENTRIES );
	return true;
}

void CLightingManager::Shutdown()
{
	if ( cl_entitylist )
	{
		cl_entitylist->RemoveListenerEntity( this );
	}
	if ( m_pCachedPropStates )
	{
		delete[] m_pCachedPropStates;
		m_pCachedPropStates = NULL;
	}

	for ( int i = m_hDeferredTempLights.Count() - 1; i >= 0; i-- )
	{
		def_light_temp_t* light = m_hDeferredTempLights[i];
		RemoveLight( light );
		delete light;
	}
	m_hDeferredTempLights.Purge();

	Assert( m_hDeferredLights.Count() == 0 );
	materials->RemoveReleaseFunc( LightingResourceRelease );
	Q_memset( s_bAtlasSlotUsed, 0, sizeof( s_bAtlasSlotUsed ) );
	Q_memset( s_bAtlasSlotUsed_Point, 0, sizeof( s_bAtlasSlotUsed_Point ) );

#if DEFCFG_USE_SSE
	if ( m_pSortDataX4 != NULL )
	{
		MemAlloc_FreeAligned( m_pSortDataX4 );
		m_pSortDataX4 = NULL;
	}
#endif
}

void CLightingManager::LevelInitPostEntity()
{
	Q_memset( s_bAtlasSlotUsed, 0, sizeof( s_bAtlasSlotUsed ) );
	Q_memset( s_bAtlasSlotUsed_Point, 0, sizeof( s_bAtlasSlotUsed_Point ) );
	if ( m_pCachedPropStates )
	{
		Q_memset( m_pCachedPropStates, 0, sizeof( CachedPropState_t ) * NUM_ENT_ENTRIES );
	}

	for ( int i = 0; i < m_hDeferredLights.Count(); i++ )
	{
		def_light_t *l = m_hDeferredLights[i];
		if ( !l )
			continue;

		l->m_nCachedAtlasSlot = -1;
		if ( r_deferred_shadow_cache.GetBool() && l->IsStatic() && ( l->IsSpot() || l->IsPoint() ) )
		{
			AssignStaticAtlasSlot( l );
			l->SetShadowDirty( true );
		}
	}
}

void CLightingManager::LevelShutdownPostEntity()
{
	for ( int i = m_hDeferredTempLights.Count() - 1; i >= 0; i-- )
	{
		def_light_temp_t* light = m_hDeferredTempLights[i];
		RemoveLight( light );
		delete light;
	}
	m_hDeferredTempLights.Purge();

	m_hRenderLights.Purge();

	for ( int i = 0; i < LSORT_COUNT; i++ )
		m_hPreSortedLights[i].Purge();

	for ( int i = 0; i < m_hDeferredLights.Count(); i++ )
	{
		if ( m_hDeferredLights[i] )
			m_hDeferredLights[i]->m_nCachedAtlasSlot = -1;
	}

	Q_memset( s_bAtlasSlotUsed, 0, sizeof( s_bAtlasSlotUsed ) );
	Q_memset( s_bAtlasSlotUsed_Point, 0, sizeof( s_bAtlasSlotUsed_Point ) );
	if ( m_pCachedPropStates )
	{
		Q_memset( m_pCachedPropStates, 0, sizeof( CachedPropState_t ) * NUM_ENT_ENTRIES );
	}
}

int CLightingManager::AssignStaticAtlasSlot( def_light_t *l )
{
	if ( !l )
		return -1;

	FreeStaticAtlasSlot( l );

	if ( l->IsPoint() )
	{
		for ( int i = 0; i < MAX_STATIC_POINT_SHADOW_SLOTS; i++ )
		{
			if ( !s_bAtlasSlotUsed_Point[i] )
			{
				s_bAtlasSlotUsed_Point[i] = true;
				l->m_nCachedAtlasSlot = i;
				return i;
			}
		}
	}
	else if ( l->IsSpot() )
	{
		for ( int i = 0; i < MAX_STATIC_SHADOW_ATLAS_SLOTS; i++ )
		{
			if ( !s_bAtlasSlotUsed[i] )
			{
				s_bAtlasSlotUsed[i] = true;
				l->m_nCachedAtlasSlot = i;
				return i;
			}
		}
	}

	l->m_nCachedAtlasSlot = -1;
	return -1;
}

void CLightingManager::FreeStaticAtlasSlot( def_light_t *l )
{
	if ( !l || !l->HasValidAtlasSlot() )
		return;

	if ( l->IsPoint() )
	{
		if ( l->m_nCachedAtlasSlot >= 0 && l->m_nCachedAtlasSlot < MAX_STATIC_POINT_SHADOW_SLOTS )
		{
			s_bAtlasSlotUsed_Point[ l->m_nCachedAtlasSlot ] = false;
		}
	}
	else if ( l->IsSpot() )
	{
		if ( l->m_nCachedAtlasSlot >= 0 && l->m_nCachedAtlasSlot < MAX_STATIC_SHADOW_ATLAS_SLOTS )
		{
			s_bAtlasSlotUsed[ l->m_nCachedAtlasSlot ] = false;
		}
	}

	l->m_nCachedAtlasSlot = -1;
}

int CLightingManager::GetStaticAtlasSlotCount() const
{
	int count = 0;
	for ( int i = 0; i < MAX_STATIC_SHADOW_ATLAS_SLOTS; i++ )
	{
		if ( s_bAtlasSlotUsed[i] )
			count++;
	}
	return count;
}

int CLightingManager::GetStaticPointAtlasSlotCount() const
{
	int count = 0;
	for ( int i = 0; i < MAX_STATIC_POINT_SHADOW_SLOTS; i++ )
	{
		if ( s_bAtlasSlotUsed_Point[i] )
			count++;
	}
	return count;
}

void CLightingManager::ReevaluateStaticAtlasSlots()
{
	if ( r_deferred_shadow_cache.GetBool() )
	{
		if ( m_pCachedPropStates )
		{
			Q_memset( m_pCachedPropStates, 0, sizeof( CachedPropState_t ) * NUM_ENT_ENTRIES );
		}
		for ( int i = 0; i < m_hDeferredLights.Count(); i++ )
		{
			def_light_t *l = m_hDeferredLights[i];
			if ( l->IsStatic() && ( l->IsSpot() || l->IsPoint() ) && !l->HasValidAtlasSlot() )
			{
				AssignStaticAtlasSlot( l );
				l->SetShadowDirty( true );
			}
		}
	}
	else
	{
		for ( int i = 0; i < m_hDeferredLights.Count(); i++ )
		{
			def_light_t *l = m_hDeferredLights[i];
			if ( l->HasValidAtlasSlot() )
			{
				FreeStaticAtlasSlot( l );
			}
		}
	}
}

static void OnDeferredShadowCacheChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	if ( GetLightingManager() )
	{
		GetLightingManager()->ReevaluateStaticAtlasSlots();
	}
}

class CAbsQueryScope {
public:
	CAbsQueryScope() : m_bPrev( C_BaseEntity::IsAbsQueriesValid() ) {
		if ( !m_bPrev ) C_BaseEntity::SetAbsQueriesValid( true );
	}
	~CAbsQueryScope() {
		if ( !m_bPrev ) C_BaseEntity::SetAbsQueriesValid( false );
	}
private:
	bool m_bPrev;
};

bool CLightingManager::DoesLightIntersectBox( def_light_t *l, const Vector &vecCenter, float flPropRadius, const Vector &mins, const Vector &maxs )
{
	if ( !l )
		return false;

	const Vector vecToProp = vecCenter - l->pos;
	const float flDistSqr = vecToProp.LengthSqr();
	const float flMaxDist = l->flRadius + flPropRadius;
	if ( flDistSqr > ( flMaxDist * flMaxDist ) )
		return false;

	if ( l->IsSpot() )
	{
		const float flDist = FastSqrt( flDistSqr );
		if ( flDist > 1e-3f )
		{
			const Vector dirToProp = vecToProp * ( 1.0f / flDist );
			const float flDot = DotProduct( -l->backDir, dirToProp );
			const float flAngularSlop = ( flDist > flPropRadius ) ? ( flPropRadius / flDist ) : 1.0f;
			if ( flDot < ( l->flSpotCone_Outer - flAngularSlop ) )
				return false;
		}

		if ( R_CullBox( mins, maxs, l->spotFrustum ) )
			return false;
	}
	else
	{
		if ( !IsBoxIntersectingBox( mins, maxs, l->bounds_min_naive, l->bounds_max_naive ) )
			return false;
	}

	CTraceFilterWorldOnly filter;
	trace_t tr;
	UTIL_TraceLine( l->pos, vecCenter, MASK_OPAQUE, &filter, &tr );
	if ( tr.fraction >= 1.0f || tr.startsolid )
		return true;

	const Vector testPoints[4] = {
		Vector( mins.x, mins.y, maxs.z ),
		Vector( maxs.x, maxs.y, maxs.z ),
		Vector( mins.x, maxs.y, mins.z ),
		Vector( maxs.x, mins.y, mins.z )
	};
	for ( int p = 0; p < 4; p++ )
	{
		UTIL_TraceLine( l->pos, testPoints[p], MASK_OPAQUE, &filter, &tr );
		if ( tr.fraction >= 1.0f )
			return true;
	}

	return false;
}

bool CLightingManager::DoesLightIntersectEntity( def_light_t *l, C_BaseEntity *pEnt )
{
	if ( !l || !pEnt )
		return false;

	CAbsQueryScope scope;

	Vector mins, maxs;
	pEnt->GetRenderBoundsWorldspace( mins, maxs );

	const Vector propCenter = pEnt->WorldSpaceCenter();
	const float flPropRadius = pEnt->BoundingRadius();

	return DoesLightIntersectBox( l, propCenter, flPropRadius, mins, maxs );
}

void CLightingManager::OnEntityCreated( C_BaseEntity *pEntity )
{
	if ( !pEntity )
		return;

	const int entIdx = pEntity->entindex();
	if ( entIdx >= 0 && entIdx < NUM_ENT_ENTRIES && m_pCachedPropStates )
	{
		m_pCachedPropStates[entIdx].bActive = false;
	}
}

void CLightingManager::OnEntityDeleted( C_BaseEntity *pEnt )
{
	OnEntityRemoved( pEnt );
}

void CLightingManager::OnEntityRemoved( C_BaseEntity *pEnt )
{
	if ( !r_deferred_shadow_cache.GetBool() || !pEnt )
		return;

	const int entIdx = pEnt->entindex();
	if ( entIdx >= 0 && entIdx < NUM_ENT_ENTRIES && m_pCachedPropStates && m_pCachedPropStates[entIdx].bActive )
	{
		const Vector oldCenter = m_pCachedPropStates[entIdx].vecCenter;
		const float flRad = m_pCachedPropStates[entIdx].flRadius;
		const Vector oldMins = oldCenter - Vector( flRad, flRad, flRad );
		const Vector oldMaxs = oldCenter + Vector( flRad, flRad, flRad );

		for ( int i = 0; i < m_hDeferredLights.Count(); i++ )
		{
			def_light_t *l = m_hDeferredLights[i];
			if ( l && l->IsStatic() && ( l->IsSpot() || l->IsPoint() ) && l->HasValidAtlasSlot() && !l->IsShadowDirty() )
			{
				if ( DoesLightIntersectBox( l, oldCenter, flRad, oldMins, oldMaxs ) )
				{
					l->SetShadowDirty( true );
				}
			}
		}
		m_pCachedPropStates[entIdx].bActive = false;
		return;
	}

	if ( pEnt->IsPlayer() || dynamic_cast<C_BaseViewModel*>( pEnt ) != NULL || pEnt->IsBaseCombatWeapon() )
		return;

	if ( !pEnt->GetModel() )
		return;

	for ( int i = 0; i < m_hDeferredLights.Count(); i++ )
	{
		def_light_t *l = m_hDeferredLights[i];
		if ( l && l->IsStatic() && ( l->IsSpot() || l->IsPoint() ) && l->HasValidAtlasSlot() && !l->IsShadowDirty() )
		{
			if ( DoesLightIntersectEntity( l, pEnt ) )
			{
				l->SetShadowDirty( true );
			}
		}
	}
}

void CLightingManager::CheckDynamicEntityIntersections()
{
	if ( !r_deferred_shadow_cache.GetBool() || !engine->IsInGame() || engine->IsLevelMainMenuBackground() )
		return;

	if ( !m_pCachedPropStates )
		return;

	CAbsQueryScope scope;

	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();

	const int maxEnts = cl_entitylist->GetHighestEntityIndex();
	for ( int i = 1; i <= maxEnts; i++ )
	{
		C_BaseEntity *pEnt = cl_entitylist->GetBaseEntity( i );
		if ( !pEnt || pEnt->IsDormant() )
		{
			if ( i < NUM_ENT_ENTRIES && m_pCachedPropStates[i].bActive )
			{
				const Vector oldCenter = m_pCachedPropStates[i].vecCenter;
				const float flRad = m_pCachedPropStates[i].flRadius;
				const Vector oldMins = oldCenter - Vector( flRad, flRad, flRad );
				const Vector oldMaxs = oldCenter + Vector( flRad, flRad, flRad );

				for ( int lIdx = 0; lIdx < m_hDeferredLights.Count(); lIdx++ )
				{
					def_light_t *l = m_hDeferredLights[lIdx];
					if ( l && l->IsStatic() && ( l->IsSpot() || l->IsPoint() ) && l->HasValidAtlasSlot() && !l->IsShadowDirty() )
					{
						if ( DoesLightIntersectBox( l, oldCenter, flRad, oldMins, oldMaxs ) )
						{
							l->SetShadowDirty( true );
						}
					}
				}
				m_pCachedPropStates[i].bActive = false;
			}
			continue;
		}

		if ( pEnt->IsPlayer() )
			continue;

		if ( dynamic_cast<C_BaseViewModel*>( pEnt ) != NULL )
			continue;

		if ( pEnt->IsBaseCombatWeapon() )
			continue;

		if ( pLocal )
		{
			if ( pEnt == pLocal || pEnt->GetMoveParent() == pLocal || pEnt->GetRootMoveParent() == pLocal )
				continue;
		}

		if ( pEnt->IsEffectActive( EF_NODRAW | EF_NOSHADOW ) )
			continue;

		if ( pEnt->ShadowCastType() == SHADOWS_NONE )
			continue;

		if ( !pEnt->GetModel() )
			continue;

		const int entIdx = pEnt->entindex();
		if ( entIdx < 0 || entIdx >= NUM_ENT_ENTRIES )
			continue;

		CachedPropState_t &state = m_pCachedPropStates[entIdx];

		const Vector curOrigin = pEnt->GetAbsOrigin();
		const QAngle curAngles = pEnt->GetAbsAngles();

		IPhysicsObject *pPhys = pEnt->VPhysicsGetObject();
		const bool bHeld = ( pPhys && ( pPhys->GetGameFlags() & FVPHYSICS_PLAYER_HELD ) != 0 ) ||
			( pEnt->GetOwnerEntity() && pEnt->GetOwnerEntity()->IsPlayer() );

		bool bPoseChanged = false;
		if ( !state.bActive )
		{
			bPoseChanged = true;
		}
		else
		{
			const float flDistSqr = ( curOrigin - state.vecOrigin ).LengthSqr();
			const float flAngDiff = ( curAngles[0] - state.angAngles[0] ) * ( curAngles[0] - state.angAngles[0] ) +
			                        ( curAngles[1] - state.angAngles[1] ) * ( curAngles[1] - state.angAngles[1] ) +
			                        ( curAngles[2] - state.angAngles[2] ) * ( curAngles[2] - state.angAngles[2] );
			if ( bHeld || flDistSqr > 0.25f || flAngDiff > 0.25f )
			{
				bPoseChanged = true;
			}
		}

		if ( bPoseChanged )
		{
			Vector mins, maxs;
			pEnt->GetRenderBoundsWorldspace( mins, maxs );
			const Vector propCenter = pEnt->WorldSpaceCenter();
			const float flCurRadius = ( maxs - mins ).Length() * 0.5f;
			if ( flCurRadius < 1.0f )
				continue;

			const Vector oldCenter = state.vecCenter;
			const float flOldRad = state.flRadius;
			const Vector oldMins = oldCenter - Vector( flOldRad, flOldRad, flOldRad );
			const Vector oldMaxs = oldCenter + Vector( flOldRad, flOldRad, flOldRad );

			for ( int lIdx = 0; lIdx < m_hDeferredLights.Count(); lIdx++ )
			{
				def_light_t *l = m_hDeferredLights[lIdx];
				if ( !l || !l->IsStatic() || ( !l->IsSpot() && !l->IsPoint() ) || !l->HasValidAtlasSlot() )
					continue;

				if ( state.bActive && l->IsShadowDirty() )
					continue;

				bool bHit = DoesLightIntersectBox( l, propCenter, flCurRadius, mins, maxs );

				if ( !bHit && state.bActive )
				{
					if ( DoesLightIntersectBox( l, oldCenter, flOldRad, oldMins, oldMaxs ) )
					{
						bHit = true;
					}
				}

				if ( bHit )
				{
					l->SetShadowDirty( true );
				}
			}

			state.vecOrigin = curOrigin;
			state.vecCenter = propCenter;
			state.angAngles = curAngles;
			state.flRadius  = flCurRadius;
			state.bActive   = true;
		}
	}
}

void CLightingManager::Update( float ft )
{
}

void CLightingManager::SetRenderWorldLights( bool bRender )
{
	m_bDrawWorldLights = bRender;
}

void CLightingManager::SetRenderConstants( const VMatrix&    ScreenToWorld,
                                           const CViewSetup& setup )
{
	m_matScreenToWorld = ScreenToWorld;

	m_vecViewOrigin = setup.origin;
	AngleVectors( setup.angles, &m_vecForward );

	m_flzNear = setup.zNear;
}

void CLightingManager::LightSetup( const CViewSetup& setup )
{
	m_vecViewOrigin = setup.origin;
	AngleVectors( setup.angles, &m_vecForward );
	m_flzNear = setup.zNear;

	// Remove lights that have run out of time
	UpdateTemplights( gpGlobals->frametime );

	PrepareLights();

	if ( r_deferred_shadow_cache.GetBool() )
	{
		CheckDynamicEntityIntersections();
	}

#if DEBUG
	m_bVolatileLists = true;
#endif

	CullLights();

#if DEFCFG_USE_SSE
	if ( m_bSortDataNeedsRealloc )
	{
		AllocateSortDataBuffer();
		m_bSortDataNeedsRealloc = false;
	}

	BuildLightSortDataBuffer();
#endif

	SortLights();

	if ( r_deferred_light_stats.GetInt() >= 2 )
	{
		DebugLights_Draw_Boundingboxes();
	}
}

void CLightingManager::LightTearDown()
{
	DoSceneDebug();

	ClearTmpLists();

#if DEBUG
	m_bVolatileLists = false;
#endif
}

void CLightingManager::OnCookieStringReceived( const char* pszString, const int& index )
{
	ITexture* pTexCookie = materials->FindTexture( pszString, TEXTURE_GROUP_OTHER );

	if ( IsErrorTexture( pTexCookie ) )
		return;
}

void CLightingManager::OnMaterialReload()
{
	FOR_EACH_VEC_FAST( def_light_t*, m_hDeferredLights, l )
	{
		l->ClearCookie();
		l->MakeDirtyAll();
		l->SetShadowDirty( true );
	}
	FOR_EACH_VEC_FAST_END
}

void CLightingManager::AddLight( def_light_t* l )
{
	Assert( !m_hDeferredLights.HasElement( l ) );

	m_hDeferredLights.AddToTail( l );

	if ( r_deferred_shadow_cache.GetBool() && l->IsStatic() && ( l->IsSpot() || l->IsPoint() ) && !l->HasValidAtlasSlot() )
	{
		AssignStaticAtlasSlot( l );
		l->SetShadowDirty( true );
	}

#if DEFCFG_USE_SSE
	m_bSortDataNeedsRealloc = true;
#endif
}

bool CLightingManager::RemoveLight( def_light_t* l )
{
	FreeStaticAtlasSlot( l );
#if DEBUG
	AssertMsg( m_bVolatileLists == false, "You MUST NOT remove lights while rendering." );
#endif

#if DEFCFG_USE_SSE
	const bool bSuccess = m_hDeferredLights.FindAndRemove( l );
	//if ( bSuccess )
	//	m_bSortDataNeedsRealloc = true;
	return bSuccess;
#else
	return m_hDeferredLights.FindAndRemove( l );
#endif
}

bool CLightingManager::IsLightRendered( def_light_t* l ) const
{
#if DEBUG
	AssertMsg( m_bVolatileLists, "Only works correctly during rendering." );
#endif

	return m_hRenderLights.HasElement( l );
}

int CLightingManager::CountTempLights() const
{
	return m_hDeferredTempLights.Count();
}

void CLightingManager::AddTempLight( def_light_temp_t* l )
{
	l->pos = SanitizeLightOrigin( l->pos );
	l->iShadow_Dist = l->iVisible_Dist;
	l->iShadow_Range = l->iVisible_Range;
	l->flStartRadius = l->flRadius;
	l->vecStartColor = l->col_diffuse;
	l->MakeDirtyAll();
	m_hDeferredTempLights.AddToTail( l );
	AddLight( l );
}

void CLightingManager::UpdateTemplights( float deltaTime )
{
	for ( int i = m_hDeferredTempLights.Count() - 1; i >= 0; i-- )
	{
		def_light_temp_t* light = m_hDeferredTempLights[i];
		if ( light->fEndLifeTime <= gpGlobals->curtime || light->fLifeDuration <= 0.0f )
		{
			RemoveLight( light );
			m_hDeferredTempLights.Remove( i );
			delete light;
		}
		else
		{
			const float flFrac = clamp( ( light->fEndLifeTime - gpGlobals->curtime ) / light->fLifeDuration, 0.0f, 1.0f );
			light->flRadius = light->flStartRadius * flFrac;
			light->col_diffuse = light->vecStartColor * flFrac;
		}
	}
}

Vector CLightingManager::SanitizeLightOrigin( const Vector &vecOrigin, const Vector *pFallback, float flMinClearance )
{
	Vector safeOrigin = vecOrigin;

	if ( ( enginetrace->GetPointContents( safeOrigin ) & MASK_SOLID ) != 0 )
	{
		if ( pFallback && ( ( enginetrace->GetPointContents( *pFallback ) & MASK_SOLID ) == 0 ) )
		{
			safeOrigin = *pFallback;
		}
		else
		{
			static const Vector s_probeOffsets[] = {
				Vector( 0, 0, 8 ), Vector( 0, 0, -8 ),
				Vector( 8, 0, 0 ), Vector( -8, 0, 0 ),
				Vector( 0, 8, 0 ), Vector( 0, -8, 0 ),
				Vector( 16, 0, 0 ), Vector( -16, 0, 0 ),
				Vector( 0, 16, 0 ), Vector( 0, -16, 0 ),
				Vector( 0, 0, 16 ), Vector( 0, 0, -16 )
			};
			for ( int i = 0; i < ARRAYSIZE( s_probeOffsets ); ++i )
			{
				Vector testPos = vecOrigin + s_probeOffsets[i];
				if ( ( enginetrace->GetPointContents( testPos ) & MASK_SOLID ) == 0 )
				{
					safeOrigin = testPos;
					break;
				}
			}
		}
	}

	CTraceFilterWorldOnly worldFilter;
	const Vector hullMins( -flMinClearance, -flMinClearance, -flMinClearance );
	const Vector hullMaxs( flMinClearance, flMinClearance, flMinClearance );
	Vector prevNormal = vec3_origin;

	for ( int i = 0; i < 3; ++i )
	{
		trace_t tr;
		UTIL_TraceHull( safeOrigin, safeOrigin, hullMins, hullMaxs, MASK_SOLID_BRUSHONLY, &worldFilter, &tr );
		if ( !tr.startsolid && tr.fraction >= 1.0f )
			break;

		if ( tr.plane.normal.LengthSqr() < 0.001f )
		{
			static const Vector s_checkDirs[] = {
				Vector( 0, 0, -1 ), Vector( 0, 0, 1 ),
				Vector( 1, 0, 0 ), Vector( -1, 0, 0 ),
				Vector( 0, 1, 0 ), Vector( 0, -1, 0 )
			};
			for ( int j = 0; j < ARRAYSIZE( s_checkDirs ); ++j )
			{
				trace_t rayTr;
				UTIL_TraceLine( safeOrigin, safeOrigin + s_checkDirs[j] * flMinClearance, MASK_SOLID_BRUSHONLY, &worldFilter, &rayTr );
				if ( rayTr.fraction < 1.0f && !rayTr.startsolid && rayTr.plane.normal.LengthSqr() > 0.001f )
				{
					tr.plane.normal = rayTr.plane.normal;
					tr.fraction = rayTr.fraction;
					break;
				}
			}
			if ( tr.plane.normal.LengthSqr() < 0.001f )
				break;
		}

		const float pushDist = ( 1.0f - tr.fraction ) * flMinClearance + 1.0f;
		const Vector vecPush = tr.plane.normal * pushDist;

		if ( prevNormal.LengthSqr() > 0.001f && DotProduct( tr.plane.normal, prevNormal ) < -0.7f )
		{
			safeOrigin -= vecPush * 0.5f;
			break;
		}

		safeOrigin += vecPush;
		prevNormal = tr.plane.normal;
	}

	return safeOrigin;
}

def_light_t *CLightingManager::AllocDynamicPointLight( const Vector &vecOrigin, const Vector &vecColor, float flRadius,
	int iFlags, float flVisibleDist, const Vector *pFallbackOrigin, C_BaseEntity *pIgnoreEntity )
{
	Vector safeOrigin = SanitizeLightOrigin( vecOrigin, pFallbackOrigin );

	def_light_t *l = new def_light_t();
	l->iLighttype = DEFLIGHTTYPE_POINT;
	l->pos = safeOrigin;
	l->ang = vec3_angle;
	l->flRadius = flRadius;
	l->col_diffuse = vecColor;
	l->flFalloffPower = 1.0f;
	l->iFlags |= iFlags;
	l->iVisible_Dist = (uint16)clamp( flVisibleDist, 256.0f, 32768.0f );
	l->iVisible_Range = 512;
	l->iShadow_Dist = l->iVisible_Dist;
	l->iShadow_Range = l->iVisible_Range;
	l->m_hIgnoreEntity = pIgnoreEntity;
	l->MakeDirtyAll();

	AddLight( l );
	return l;
}

def_light_t *CLightingManager::AllocDynamicSpotLight( const Vector &vecOrigin, const QAngle &vecAngles, const Vector &vecColor,
	float flRadius, float flInnerConeDeg, float flOuterConeDeg, int iFlags, float flVisibleDist, const Vector *pFallbackOrigin,
	C_BaseEntity *pIgnoreEntity )
{
	Vector safeOrigin = SanitizeLightOrigin( vecOrigin, pFallbackOrigin );

	def_light_t *l = new def_light_t();
	l->iLighttype = DEFLIGHTTYPE_SPOT;
	l->pos = safeOrigin;
	l->ang = vecAngles;
	l->flRadius = flRadius;
	l->col_diffuse = vecColor;
	l->flSpotCone_Outer = SPOT_DEGREE_TO_RAD( flOuterConeDeg );
	l->flSpotCone_Inner = ( flInnerConeDeg > 0.0f && flInnerConeDeg <= flOuterConeDeg ) ? SPOT_DEGREE_TO_RAD( flInnerConeDeg ) : l->flSpotCone_Outer;
	l->flFOV = flOuterConeDeg * 2.0f;
	l->flFalloffPower = 1.0f;
	l->iFlags |= iFlags;
	l->iVisible_Dist = (uint16)clamp( flVisibleDist, 256.0f, 32768.0f );
	l->iVisible_Range = 512;
	l->iShadow_Dist = l->iVisible_Dist;
	l->iShadow_Range = l->iVisible_Range;
	l->m_hIgnoreEntity = pIgnoreEntity;
	l->MakeDirtyAll();

	AddLight( l );
	return l;
}

void CLightingManager::UpdateDynamicLight( def_light_t *pLight, const Vector &vecOrigin, const Vector &vecColor, float flRadius,
	const QAngle *pAngles, bool bSanitizeOrigin, const Vector *pFallbackOrigin, C_BaseEntity *pIgnoreEntity )
{
	if ( !pLight )
		return;

	Vector safeOrigin = bSanitizeOrigin ? SanitizeLightOrigin( vecOrigin, pFallbackOrigin ) : vecOrigin;

	bool bDirtyXForms = false;
	if ( ( pLight->pos - safeOrigin ).LengthSqr() > 0.01f )
	{
		pLight->pos = safeOrigin;
		bDirtyXForms = true;
	}

	if ( pAngles && ( pLight->ang != *pAngles ) )
	{
		pLight->ang = *pAngles;
		bDirtyXForms = true;
	}

	if ( pLight->flRadius != flRadius )
	{
		pLight->flRadius = flRadius;
		bDirtyXForms = true;
	}

	pLight->col_diffuse = vecColor;

	if ( pIgnoreEntity )
	{
		pLight->m_hIgnoreEntity = pIgnoreEntity;
	}

	if ( bDirtyXForms )
	{
		pLight->MakeDirtyXForms();
	}
}

bool CLightingManager::EnsureDynamicPointLight( def_light_t *&pLight, const Vector &vecOrigin, const Vector &vecColor, float flRadius,
	int iFlags, float flVisibleDist, const Vector *pFallbackOrigin, C_BaseEntity *pIgnoreEntity )
{
	if ( !pLight )
	{
		pLight = AllocDynamicPointLight( vecOrigin, vecColor, flRadius, iFlags, flVisibleDist, pFallbackOrigin, pIgnoreEntity );
		return true;
	}

	UpdateDynamicLight( pLight, vecOrigin, vecColor, flRadius, NULL, true, pFallbackOrigin, pIgnoreEntity );
	return false;
}

bool CLightingManager::EnsureDynamicSpotLight( def_light_t *&pLight, const Vector &vecOrigin, const QAngle &vecAngles, const Vector &vecColor,
	float flRadius, float flInnerConeDeg, float flOuterConeDeg, int iFlags, float flVisibleDist, const Vector *pFallbackOrigin,
	C_BaseEntity *pIgnoreEntity )
{
	if ( !pLight )
	{
		pLight = AllocDynamicSpotLight( vecOrigin, vecAngles, vecColor, flRadius, flInnerConeDeg, flOuterConeDeg, iFlags, flVisibleDist, pFallbackOrigin, pIgnoreEntity );
		return true;
	}

	UpdateDynamicLight( pLight, vecOrigin, vecColor, flRadius, &vecAngles, true, pFallbackOrigin, pIgnoreEntity );
	return false;
}

void CLightingManager::FreeDynamicLight( def_light_t *&pLight )
{
	if ( pLight )
	{
		RemoveLight( pLight );
		delete pLight;
		pLight = NULL;
	}
}

ConVar r_deferred_light_distance_fade(
	"r_deferred_light_distance_fade",
	"0",
	FCVAR_ARCHIVE,
	"distance attenuation fading for deferred dynamic lights"
);

ConVar r_deferred_shadow_dist_min(
	"r_deferred_shadow_dist_min",
	"4096",
	FCVAR_ARCHIVE,
	"minimum distance for dynamic light shadow fade"
);

ConVar r_deferred_shadow_max(
	"r_deferred_shadow_max",
	"64",
	FCVAR_ARCHIVE,
	"maximum simultaneous dynamic shadow casters"
);

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
			lights[0]->HasShadow() ? 1 : 0,
			lights[1]->HasShadow() ? 1 : 0,
			lights[2]->HasShadow() ? 1 : 0,
			lights[3]->HasShadow() ? 1 : 0
		};
		sortData.hasShadow = LoadUnalignedIntSIMD( hasShadowX4 );

		int hasVolumetricsX4[4] =
		{
			lights[0]->HasVolumetrics() ? 1 : 0,
			lights[1]->HasVolumetrics() ? 1 : 0,
			lights[2]->HasVolumetrics() ? 1 : 0,
			lights[3]->HasVolumetrics() ? 1 : 0
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

			hasShadowX4[i] = l->HasShadow() ? 1 : 0;

			hasVolumetricsX4[i] = l->HasVolumetrics() ? 1 : 0;
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

void CLightingManager::ClearTmpLists()
{
	m_hRenderLights.RemoveAll();

	for ( int i = 0; i < LSORT_COUNT; i++ )
		m_hPreSortedLights[i].RemoveAll();
}

void CLightingManager::PrepareLights()
{
	FOR_EACH_VEC_FAST( def_light_t*, m_hDeferredLights, l )
	{
		l->UpdateCookieTexture();

		if ( !l->IsDirty() )
		{
			continue;
		}

		if ( l->IsDirtyXForms() )
		{
			if ( l->IsSpot() )
				l->UpdateMatrix();

			l->UpdateXForms();
		}

		if ( l->IsDirtyRenderMesh() )
		{
			l->UpdateRenderMesh();
			l->UpdateVolumetrics();
		}

		l->UnDirtyAll();
	}
	FOR_EACH_VEC_FAST_END
}

void CLightingManager::CullLights()
{
	Assert( m_hRenderLights.Count() == 0 );

	FOR_EACH_VEC_FAST( def_light_t*, m_hDeferredLights, l )
	{
		if ( !m_bDrawWorldLights && l->bWorldLight )
			continue;

		if ( engine->CullBox( l->bounds_min_naive, l->bounds_max_naive ) )
			continue;

		Vector vecClosest;
		CalcClosestPointOnAABB( l->bounds_min_naive, l->bounds_max_naive, m_vecViewOrigin, vecClosest );
		Vector veclightDelta = vecClosest - m_vecViewOrigin;

		if ( r_deferred_light_distance_fade.GetBool() && ( veclightDelta.LengthSqr() > l->flMaxDistSqr ) )
			continue;

		const float flEffectiveShadowDist  = ( r_deferred_shadow_dist_min.GetFloat() > 0.0f )
			                                   ? r_deferred_shadow_dist_min.GetFloat()
			                                   : (float)l->iShadow_Dist;
		const float flEffectiveShadowRange = ( l->iShadow_Range > 0 ) ? (float)l->iShadow_Range : 1024.0f;

		l->flDistance_ViewOrigin = veclightDelta.Length();
		l->flShadowFade          = l->HasShadow()
			                           ? ( SATURATE( ( l->flDistance_ViewOrigin - flEffectiveShadowDist ) / flEffectiveShadowRange ) )
			                           : 1.0f;

		m_hRenderLights.AddToTail( l );
	}
	FOR_EACH_VEC_FAST_END
}

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
				const bool bNeedsFullscreen = !R_CullBox( camMins, camMaxs, pLight->spotFrustum );
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

FORCEINLINE float CLightingManager::DoLightStyle( def_light_t* l )
{
	if ( !l->HasLightstyle() )
		return 1.0f;

	if ( gpGlobals->curtime > l->flLastRandomTime )
	{
		l->flLastRandomTime = gpGlobals->curtime + 1.0f - Min( 1.f, l->flStyle_Speed );

		random->SetSeed( l->iStyleSeed );
		l->flLastRandomValue = 1.0f - random->RandomFloat( 0, Min( 1.f, l->flStyle_Amount ) );
	}

	const float ran = l->flLastRandomValue;

	float sine = FastCos( gpGlobals->curtime * l->flStyle_Speed + l->iStyleSeed ) * 0.5f + 0.5f;
	sine       = Bias( sine, l->flStyle_Smooth );

	return Lerp( l->flStyle_Random, Lerp( l->flStyle_Amount, 1.0f, sine ), ran );
}

/* Which data to commit and how:

	Point lights:

	position		x y z	radius			w
	color diffuse	x y z	falloffpower	w
	color ambient	x y z

	** shadowed or cookied **
							shadowscale		w

	rotationmatrix	x y z
					x y z
					x y z

	max num constants: 10 * 3 +				-- simple
						3 * 6 +				-- cookie
						3 * 6 +				-- shadowed
						2 * 6				-- shadowed cookie


	Spot lights:

	position			x y z	radius			w
	color diffuse		x y z	falloffpower	w
	color ambient		x y z	cone outer		w
	backward			x y z	cone inner		w

	** shadowed or cookied **
	shadowscale			x

	world2texture		x y z w
						x y z w
						x y z w
						x y z w

	max num constants: 10 * 4 +				-- simple
						3 * 9 +				-- cookie
						3 * 9 +				-- shadowed
						2 * 9				-- shadowed cookie
	*/

FORCEINLINE int CLightingManager::WriteLight( def_light_t* l, float* pfl4, int bShadow, int bCookie )
{
	const bool bShadowActual = ( bShadow >= 0 ) ? ( bShadow != 0 ) : l->ShouldRenderShadow();
	const bool bCookieActual = ( bCookie >= 0 ) ? ( bCookie != 0 ) : ( l->HasCookie() && l->IsCookieReady() );
	const bool bAdvanced = bShadowActual || bCookieActual || l->HasUberlight();

	int numConsts = 0;

	const float flLightstyle = DoLightStyle( l );
	const float flDistance   = l->flDistance_ViewOrigin;
	const float flMasterFade = r_deferred_light_distance_fade.GetBool()
		                           ? ( flLightstyle * ( 1.0f - SATURATE( ( flDistance - l->iVisible_Dist ) / l->iVisible_Range ) ) )
		                           : flLightstyle;

	const int iFloatSize = sizeof( float );

	const Vector& colDiffuse = l->col_diffuse * flMasterFade;
	const Vector& colAmbient = l->col_ambient * flMasterFade;

	switch ( l->iLighttype )
	{
	default:
		Assert(0);
	case DEFLIGHTTYPE_POINT:
		{
			numConsts = bAdvanced ? NUM_CONSTS_POINT_ADVANCED : NUM_CONSTS_POINT_SIMPLE;

			Q_memcpy( pfl4, l->pos.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flRadius;
			pfl4++;

			Q_memcpy( pfl4, colDiffuse.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flFalloffPower;
			pfl4++;

			Q_memcpy( pfl4, colAmbient.Base(), iFloatSize * 3 );
			pfl4 += 3;

			if ( bAdvanced )
			{
				*pfl4 = l->flShadowFade;
				pfl4++;

				VMatrix rotMatrix, rotMatrixit;
				rotMatrix.Identity();
				rotMatrix.SetupMatrixOrgAngles( vec3_origin, l->ang );
				MatrixSourceToDeviceSpaceInv( rotMatrix );
				rotMatrix = rotMatrix.Transpose3x3();
				MatrixInverseGeneral( rotMatrix, rotMatrixit );
				Q_memcpy( pfl4, rotMatrixit.Base(), iFloatSize * 12 );
			}
		}
		break;
	case DEFLIGHTTYPE_SPOT:
		{
			numConsts = bAdvanced ? NUM_CONSTS_SPOT_ADVANCED : NUM_CONSTS_SPOT_SIMPLE;

			Q_memcpy( pfl4, l->pos.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flRadius;
			pfl4++;

			Q_memcpy( pfl4, colDiffuse.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flFalloffPower;
			pfl4++;

			Q_memcpy( pfl4, colAmbient.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flSpotCone_Outer;
			pfl4++;

			Q_memcpy( pfl4, l->backDir.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flSpotCone_Inner;
			pfl4++;

			if ( bAdvanced )
			{
				pfl4[0] = l->flShadowFade;
				pfl4[1] = l->HasUberlight() ? 1.0f : 0.0f;
				pfl4[2] = l->flUberRoundness;
				pfl4[3] = 0.0f;
				pfl4 += 4;

				Q_memcpy( pfl4, l->spotWorldToTex.Base(), iFloatSize * 16 );
				pfl4 += 16;

				pfl4[0] = l->flUberWidth;
				pfl4[1] = l->flUberHeight;
				pfl4[2] = l->flUberWEdge;
				pfl4[3] = l->flUberHEdge;
				pfl4 += 4;

				pfl4[0] = l->flUberShearX;
				pfl4[1] = l->flUberShearY;
				pfl4[2] = l->flUberCutOn;
				pfl4[3] = l->flUberCutOff;
				pfl4 += 4;
			}
		}
		break;
	}

	return numConsts;
}

FORCEINLINE bool SortLightsByComboType( const CUtlVector<def_light_t*>& hLights,
                                        CUtlVector<def_light_t*>& hLightsShadowedCookie,
                                        CUtlVector<def_light_t*>& hLightsShadowed,
                                        CUtlVector<def_light_t*>& hLightsCookied,
                                        CUtlVector<def_light_t*>& hLightsSimple,
                                        int nMaxShadows )
{
	hLightsShadowedCookie.RemoveAll();
	hLightsShadowed.RemoveAll();
	hLightsCookied.RemoveAll();
	hLightsSimple.RemoveAll();

	int nShadowsAssigned = 0;

	FOR_EACH_VEC_FAST( def_light_t* const, hLights, l )
	{
		const bool bWantsShadow = l->ShouldRenderShadow();
		const bool bShadowed    = bWantsShadow && ( nShadowsAssigned < nMaxShadows );
		const bool bCookie      = l->HasCookie() && l->IsCookieReady();
		const bool bUber        = l->HasUberlight();

		if ( bShadowed )
		{
			nShadowsAssigned++;
			l->bWasShadowActive = true;
		}
		else
		{
			l->bWasShadowActive = false;
		}

		if ( bShadowed && bCookie )
			hLightsShadowedCookie.AddToTail( l );
		else if ( bShadowed )
			hLightsShadowed.AddToTail( l );
		else if ( bCookie || bUber )
			hLightsCookied.AddToTail( l );
		else
			hLightsSimple.AddToTail( l );
	}
	FOR_EACH_VEC_FAST_END

	Assert( hLights.Count() ==
		hLightsShadowedCookie.Count() + hLightsShadowed.Count() +
		hLightsCookied.Count() + hLightsSimple.Count() );

	return hLights.Count() > 0;
}

FORCEINLINE void CLightingManager::DrawVolumePrepass( bool bDoModelTransform, const CViewSetup& view, def_light_t* l )
{
	if ( !l->IsSpot() )
		return;

	Assert( l->pMesh_VolumPrepass != NULL );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( GetDefRT_VolumePrepass(), 0, 0, view.width / 4, view.height / 4 );
	pRenderContext->ClearColor3ub( 0, 0, 0 );
	pRenderContext->ClearBuffers( true, false );

	if ( bDoModelTransform )
	{
		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->PushMatrix();
		pRenderContext->LoadMatrix( l->worldTransform );
	}

	pRenderContext->Bind( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_PREPASS ) );
	l->pMesh_VolumPrepass->Draw();

	if ( bDoModelTransform )
	{
		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->PopMatrix();
	}

	pRenderContext->PopRenderTargetAndViewport();
}

void CLightingManager::RenderLights( const CViewSetup& view, CDeferredViewRender* pCaller )
{
	static CUtlVector<def_light_t*> lightsShadowedCookied;
	static CUtlVector<def_light_t*> lightsShadowed;
	static CUtlVector<def_light_t*> lightsCookied;
	static CUtlVector<def_light_t*> lightsSimple;

#if DEFCFG_EXTRA_SORT
	struct Lightpass_t
	{
		CUtlVector<def_light_t*> *lightVecFullscreenSimple;
		CUtlVector<def_light_t*> *lightVecFullscreenAdvanced;
		CUtlVector<def_light_t*> *lightVecWorldSimple;
		CUtlVector<def_light_t*> *lightVecWorldAdvanced;
		IMaterial *pMatPassFullscreen;
		IMaterial *pMatPassWorld;
		IMaterial *pMatVolumeFullscreen;
		IMaterial *pMatVolumeWorld;
		int constCount_simple;
		int constCount_advanced;
	};

	Lightpass_t lightTypes[] =
	{
		{ &m_hPreSortedLights[ LSORT_POINT_FULLSCREEN_SIMPLE ], &m_hPreSortedLights[ LSORT_POINT_FULLSCREEN_ADVANCED ],
		 &m_hPreSortedLights[ LSORT_POINT_WORLD_SIMPLE ], &m_hPreSortedLights[ LSORT_POINT_WORLD_ADVANCED ],//JACK: Here we could be sorting into
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_POINT_FULLSCREEN ),						//		simple and advanced lights as
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_POINT_WORLD ),								//		simple lights only need  a single
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_POINT_FULLSCREEN ),					//		state change (model proj) to render
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_POINT_WORLD ),
		NUM_CONSTS_POINT_SIMPLE, NUM_CONSTS_POINT_ADVANCED },

		{ &m_hPreSortedLights[ LSORT_SPOT_FULLSCREEN_SIMPLE ], &m_hPreSortedLights[ LSORT_SPOT_FULLSCREEN_ADVANCED ],
		&m_hPreSortedLights[ LSORT_SPOT_WORLD_SIMPLE ], &m_hPreSortedLights[ LSORT_SPOT_WORLD_ADVANCED ],
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_SPOT_FULLSCREEN ),
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_SPOT_WORLD ),
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_SPOT_FULLSCREEN ),
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_SPOT_WORLD ),
		NUM_CONSTS_SPOT_SIMPLE, NUM_CONSTS_SPOT_ADVANCED },
	};
#else
	struct Lightpass_t
	{
		CUtlVector<def_light_t*>* lightVecFullscreen;
		CUtlVector<def_light_t*>* lightVecWorld;
		IMaterial*                pMatPassFullscreen;
		IMaterial*                pMatPassWorld;
		IMaterial*                pMatVolumeFullscreen;
		IMaterial*                pMatVolumeWorld;
		int                       constCount_simple;
		int                       constCount_advanced;
	};

	Lightpass_t lightTypes[] =
	{
		{
			&m_hPreSortedLights[LSORT_POINT_FULLSCREEN], &m_hPreSortedLights[LSORT_POINT_WORLD],
			//JACK: Here we could be sorting into
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_POINT_FULLSCREEN ), //		simple and advanced lights as
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_POINT_WORLD ),      //		simple lights only need  a single
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_POINT_FULLSCREEN ),
			//		state change (model proj) to render
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_POINT_WORLD ),
			NUM_CONSTS_POINT_SIMPLE, NUM_CONSTS_POINT_ADVANCED
		},

		{
			&m_hPreSortedLights[LSORT_SPOT_FULLSCREEN], &m_hPreSortedLights[LSORT_SPOT_WORLD],
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_SPOT_FULLSCREEN ),
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_SPOT_WORLD ),
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_SPOT_FULLSCREEN ),
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_SPOT_WORLD ),
			NUM_CONSTS_SPOT_SIMPLE, NUM_CONSTS_SPOT_ADVANCED
		},
	};
#endif
	const int iNumLightTypes = ARRAYSIZE( lightTypes );

	int iPassesDrawnFullscreen[iNumLightTypes] = { 0 };
	int iPassesVolumetrics[2]                  = { 0 };
	int iDrawnShadowed                         = 0;
	int iDrawnCookied                          = 0;
	int iDrawnSimple                           = 0;

	ITexture* pVolumBuffer0 = GetDefRT_VolumetricsBuffer( 0 );

	if ( m_bDrawVolumetrics )
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->PushRenderTargetAndViewport( pVolumBuffer0, 0, 0, view.width / 4, view.height / 4 );
		pRenderContext->ClearColor3ub( 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
	}

	struct queueVolume
	{
		queueVolume( def_light_t* l, int offset_data, int offset_sampler )
		{
			dataoffset    = offset_data;
			sampleroffset = offset_sampler;
			pLight        = l;
		};

		int          dataoffset;
		int          sampleroffset;
		def_light_t* pLight;
	};

	const int nMaxShadows = r_deferred_shadow_max.GetInt();
	for ( int i = 0; i < iNumLightTypes; i++ )
	{
		if ( SortLightsByComboType( *( lightTypes[i].lightVecFullscreen ),
		                            lightsShadowedCookied, lightsShadowed, lightsCookied, lightsSimple, nMaxShadows ) )
		{
			while ( lightsShadowedCookied.Count() > 0 || lightsShadowed.Count() > 0 ||
				lightsCookied.Count() > 0 || lightsSimple.Count() > 0 )
			{
				int drawShadowedCookied = Min( MAX_LIGHTS_SHADOWEDCOOKIE, lightsShadowedCookied.Count() );
				int drawShadowed        = Min( MAX_LIGHTS_SHADOWED, lightsShadowed.Count() );
				int drawCookied         = Min( MAX_LIGHTS_COOKIE, lightsCookied.Count() );
				int drawSimple          = Min( MAX_LIGHTS_SIMPLE, lightsSimple.Count() );

				int numRows = ( drawShadowedCookied * lightTypes[i].constCount_advanced +
					drawShadowed * lightTypes[i].constCount_advanced +
					drawCookied * lightTypes[i].constCount_advanced +
					drawSimple * lightTypes[i].constCount_simple );

				int memToAlloc = 4 * numRows;

				Assert( memToAlloc > 0 );

				lightDataCommon_t* pFlLightDataBlock = new lightDataCommon_t( memToAlloc );

				Assert( pFlLightDataBlock != NULL && pFlLightDataBlock->pFlData != NULL );

				if ( pFlLightDataBlock != NULL )
				{
					static CUtlVector<def_light_t*> commitableLights;
					static CUtlVector<queueVolume>  volumeLights;

					commitableLights.AddMultipleToTail( drawShadowedCookied, lightsShadowedCookied.Base() );
					commitableLights.AddMultipleToTail( drawShadowed, lightsShadowed.Base() );
					commitableLights.AddMultipleToTail( drawCookied, lightsCookied.Base() );
					commitableLights.AddMultipleToTail( drawSimple, lightsSimple.Base() );

					float* pFlWriter = pFlLightDataBlock->pFlData;

					for ( int k = 0; k < drawShadowedCookied; k++ )
						pFlWriter += WriteLight( lightsShadowedCookied[k], pFlWriter, 1, 1 ) * 4;

					for ( int k = 0; k < drawShadowed; k++ )
						pFlWriter += WriteLight( lightsShadowed[k], pFlWriter, 1, 0 ) * 4;

					for ( int k = 0; k < drawCookied; k++ )
						pFlWriter += WriteLight( lightsCookied[k], pFlWriter, 0, 1 ) * 4;

					for ( int k = 0; k < drawSimple; k++ )
						pFlWriter += WriteLight( lightsSimple[k], pFlWriter, 0, 0 ) * 4;

					Assert( pFlWriter - pFlLightDataBlock->pFlData == memToAlloc );

					iPassesDrawnFullscreen[i]++;

					//JACK: Here we can use the MAX_LIGHTS_X macros to mean the max light textures to use
					//		and render multiple lower LOD lights to the same texture, will need to commit the
					//		number of lights to draw, light sample lod, and texture id,

					for ( int iShadowed = 0; iShadowed < ( drawShadowedCookied + drawShadowed ); iShadowed++ )
					{
						def_light_t* pLight = commitableLights[iShadowed];
						pCaller->DrawLightShadowView( view, iShadowed, pLight );

						if ( pLight->IsSpot() )
						{
							if ( pLight->IsStatic() && pLight->HasValidAtlasSlot() )
							{
								QUEUE_FIRE( CommitTexture_ProjectedDepth, iShadowed, GetShadowColorRT_Static( pLight->m_nCachedAtlasSlot ) );
							}
							else
							{
								QUEUE_FIRE( CommitTexture_ProjectedDepth, iShadowed, GetShadowColorRT_Proj( iShadowed ) );
							}
						}
						else if ( pLight->IsPoint() )
						{
							if ( pLight->IsStatic() && pLight->HasValidAtlasSlot() )
							{
								QUEUE_FIRE( CommitTexture_DualParaboloidDepth, iShadowed, GetShadowColorRT_StaticPoint( pLight->m_nCachedAtlasSlot ) );
							}
							else
							{
								QUEUE_FIRE( CommitTexture_DualParaboloidDepth, iShadowed, GetShadowColorRT_DP( iShadowed ) );
							}
						}

						if ( pLight->HasVolumetrics() && r_deferred_volumetric_intensity.GetFloat() > 0.0f )
						{
							const int iDataOffset	 = iShadowed * lightTypes[i].constCount_advanced * 4;
							const int iSamplerOffset = iShadowed;

							volumeLights.AddToTail( queueVolume( pLight, iDataOffset, iSamplerOffset ) );
						}
					}

					commitableLights.RemoveAll();

					pFlLightDataBlock->numRows            = numRows;
					pFlLightDataBlock->numShadowedCookied = drawShadowedCookied;
					pFlLightDataBlock->numShadowed        = drawShadowed;
					pFlLightDataBlock->numCookied         = drawCookied;
					pFlLightDataBlock->numSimple          = drawSimple;
					QUEUE_FIRE( CommitLightData_Common, pFlLightDataBlock );

					iDrawnShadowed += drawShadowedCookied + drawShadowed;
					iDrawnCookied += drawShadowedCookied + drawCookied;
					iDrawnSimple += drawSimple;

					static CUtlVector<def_light_t*> cookiedLights;
					cookiedLights.AddMultipleToTail( drawShadowedCookied, lightsShadowedCookied.Base() );
					cookiedLights.AddMultipleToTail( drawCookied, lightsCookied.Base() );

					for ( int iCookied = 0; iCookied < ( drawShadowedCookied + drawCookied ); iCookied++ )
					{
						ITexture* pCookie = cookiedLights[iCookied]->GetCookieForDraw( iCookied );

						Assert( pCookie != NULL );

						QUEUE_FIRE( CommitTexture_Cookie, iCookied, pCookie );
					}

					cookiedLights.RemoveAll();

					DrawLightPassFullscreen( lightTypes[i].pMatPassFullscreen, view.width, view.height );

					FOR_EACH_VEC_FAST( queueVolume, volumeLights, entry )
					{
#if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
						const float flCameraLightDelta = (view.origin).DistTo(entry.pLight->pos);
						int iVolumeLOD = 4;
						if( flCameraLightDelta < entry.pLight->flVolumeLOD0Dist )
						{
							iVolumeLOD = 0;
						}
						else if( flCameraLightDelta < entry.pLight->flVolumeLOD1Dist )
						{
							iVolumeLOD = 1;
						}
						else if( flCameraLightDelta < entry.pLight->flVolumeLOD2Dist )
						{
							iVolumeLOD = 2;
						}
						else if( flCameraLightDelta < entry.pLight->flVolumeLOD3Dist )
						{
							iVolumeLOD = 3;
						}
#endif
						volumeData_t data;
						data.iDataOffset    = entry.dataoffset;
						data.iSamplerOffset = entry.sampleroffset;
						data.iNumRows       = lightTypes[i].constCount_advanced;
						data.bHasCookie     = entry.pLight->HasCookie();
#if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
					data.iLOD = iVolumeLOD;
#endif
#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
						data.iSamples = entry.pLight->iVolumeSamples;
#endif
						QUEUE_FIRE( CommitVolumeData, data );

						DrawVolumePrepass( true, view, entry.pLight );

						CMatRenderContextPtr pRenderContext( materials );
						pRenderContext->PushRenderTargetAndViewport( pVolumBuffer0, 0, 0, view.width / 4, view.height / 4 );

						DrawLightPassFullscreen( lightTypes[i].pMatVolumeFullscreen,
						                         view.width / 4, view.height / 4,
						                         view.width, view.height );

						pRenderContext->PopRenderTargetAndViewport();

						iPassesVolumetrics[1]++;
					}
					FOR_EACH_VEC_FAST_END

					volumeLights.RemoveAll();
				}

				lightsShadowedCookied.RemoveMultipleFromHead( drawShadowedCookied );
				lightsShadowed.RemoveMultipleFromHead( drawShadowed );
				lightsCookied.RemoveMultipleFromHead( drawCookied );
				lightsSimple.RemoveMultipleFromHead( drawSimple );
			}
		}
	}

	for ( int i = 0; i < iNumLightTypes; i++ )
	{
#if DEFCFG_EXTRA_SORT
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->Bind( lightTypes[i].pMatPassWorld );
		pRenderContext->MatrixMode( MATERIAL_MODEL );

		FOR_EACH_VEC_FAST( def_light_t*, (*(lightTypes[i].lightVecWorldSimple)), l )
		{
			if ( l->pMesh_World == NULL )
				continue;

			const bool bCookie = l->HasCookie() && l->IsCookieReady();

			int numRows = bCookie ? lightTypes[i].constCount_advanced : lightTypes[i].constCount_simple;

			int memToAlloc = 4 * numRows;

			Assert( memToAlloc > 0 );

			lightDataCommon_t *pFlLightDataBlock = new lightDataCommon_t( memToAlloc );

			WriteLight( l, pFlLightDataBlock->pFlData, 0, bCookie ? 1 : 0 );

			pFlLightDataBlock->numRows = lightTypes[i].constCount_simple;
			pFlLightDataBlock->numShadowedCookied = 0;
			pFlLightDataBlock->numShadowed = 0;
			pFlLightDataBlock->numCookied = bCookie ? 1 : 0;
			pFlLightDataBlock->numSimple = 0;
			QUEUE_FIRE( CommitLightData_Common, pFlLightDataBlock );

			if ( !bCookie )
				iDrawnSimple++;

			pRenderContext->PushMatrix();
			pRenderContext->LoadMatrix( l->worldTransform );
			l->pMesh_World->Draw();
			pRenderContext->PopMatrix();

			//ShaderStencilState_t stencilLightVolume;
		//set to reference 1 where draw succeeds
		//pRenderContext->Bind( lightTypes[i].pMatPassWorldStencilStage );
		//pMatPassWorldStencilStage only draws the backface where depth test fails
		//pRenderContext->SetStencilState( stencilLightVolume );
		//l->pMesh_World->Draw();
		//set to only draw where stencil is reference 1, set depth test to nearer
			//pRenderContext->SetStencilState( stencilLightVolume );
		}
		FOR_EACH_VEC_FAST_END

		FOR_EACH_VEC_FAST( def_light_t*, (*(lightTypes[i].lightVecWorldAdvanced)), l )
		{
			if ( l->pMesh_World == NULL )
				continue;

			const bool bShadow = l->ShouldRenderShadow();
			const bool bCookie = l->HasCookie() && l->IsCookieReady();
			const bool bUber = l->HasUberlight();

			const bool bAdvanced = ( bShadow || bCookie || bUber );

			int numRows = bAdvanced ? lightTypes[i].constCount_advanced : lightTypes[i].constCount_simple;
			int memToAlloc = 4 * numRows;

			Assert( memToAlloc > 0 );

			lightDataCommon_t *pFlLightDataBlock = new lightDataCommon_t( memToAlloc );

			WriteLight( l, pFlLightDataBlock->pFlData, bShadow ? 1 : 0, bCookie ? 1 : 0 );

			if ( bShadow )
			{
				pCaller->DrawLightShadowView( view, 0, l );

				if ( l->IsSpot() )
				{
					if ( l->IsStatic() && l->HasValidAtlasSlot() )
					{
						QUEUE_FIRE( CommitTexture_ProjectedDepth, 0, GetShadowColorRT_Static( l->m_nCachedAtlasSlot ) );
					}
					else
					{
						QUEUE_FIRE( CommitTexture_ProjectedDepth, 0, GetShadowColorRT_Proj( 0 ) );
					}
				}
				else if ( l->IsPoint() )
				{
					if ( l->IsStatic() && l->HasValidAtlasSlot() )
					{
						QUEUE_FIRE( CommitTexture_DualParaboloidDepth, 0, GetShadowColorRT_StaticPoint( l->m_nCachedAtlasSlot ) );
					}
					else
					{
						QUEUE_FIRE( CommitTexture_DualParaboloidDepth, 0, GetShadowColorRT_DP( 0 ) );
					}
				}

				iDrawnShadowed++;
			}

			pFlLightDataBlock->numRows = numRows;
			pFlLightDataBlock->numShadowedCookied = ( bShadow && bCookie ) ? 1 : 0;
			pFlLightDataBlock->numShadowed = ( bShadow && !bCookie ) ? 1 : 0;
			pFlLightDataBlock->numCookied = ( !bShadow && bCookie ) ? 1 : 0;
			pFlLightDataBlock->numSimple = bAdvanced ? 0 : 1;
			QUEUE_FIRE( CommitLightData_Common, pFlLightDataBlock );

			if ( bCookie )
			{
				ITexture* pCookie = l->GetCookieForDraw();
				Assert( pCookie != NULL );
				QUEUE_FIRE( CommitTexture_Cookie, 0, pCookie );

				iDrawnCookied++;
			}

			if ( !bShadow && !bCookie )
				iDrawnSimple++;

			pRenderContext->MatrixMode( MATERIAL_MODEL );
			pRenderContext->PushMatrix();
			pRenderContext->LoadMatrix( l->worldTransform );
			pRenderContext->Bind( lightTypes[i].pMatPassWorld );

			//ShaderStencilState_t stencilLightVolume;
		//set to reference 1 where draw succeeds
		//pRenderContext->Bind( lightTypes[i].pMatPassWorldStencilStage );
		//pMatPassWorldStencilStage only draws the backface where depth test fails
		//pRenderContext->SetStencilState( stencilLightVolume );
		//l->pMesh_World->Draw();
		//set to only draw where stencil is reference 1, set depth test to nearer
			//pRenderContext->SetStencilState( stencilLightVolume );

			l->pMesh_World->Draw();

			const bool bVolumetrics = l->HasVolumetrics() && bShadow && ( r_deferred_volumetric_intensity.GetFloat() > 0.0f );
			if ( bVolumetrics )
			{
				Assert( m_bDrawVolumetrics );
				Assert( l->pMesh_Volumetrics != NULL );

				volumeData_t data;
				data.iDataOffset    = 0;
				data.iSamplerOffset = 0;
				data.iNumRows       = lightTypes[i].constCount_advanced;
				data.bHasCookie     = bCookie;
#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
				data.iSamples = l->iVolumeSamples;
#endif
				QUEUE_FIRE( CommitVolumeData, data );

				DrawVolumePrepass( false, view, l );

				pRenderContext->PushRenderTargetAndViewport( pVolumBuffer0, 0, 0, view.width / 4, view.height / 4 );

				pRenderContext->Bind( lightTypes[i].pMatVolumeWorld );
				l->pMesh_Volumetrics->Draw();

				pRenderContext->PopRenderTargetAndViewport();

				iPassesVolumetrics[0]++;
			}

			pRenderContext->MatrixMode( MATERIAL_MODEL );
			pRenderContext->PopMatrix();
		}
		FOR_EACH_VEC_FAST_END

		pRenderContext.SafeRelease();
#else
		FOR_EACH_VEC_FAST( def_light_t*, (*(lightTypes[i].lightVecWorld)), l )
		{
			if ( l->pMesh_World == NULL )
				continue;

			const bool bShadow      = l->ShouldRenderShadow();
			const bool bCookie      = l->HasCookie() && l->IsCookieReady();
			const bool bUber        = l->HasUberlight();

			const bool bAdvanced = ( bShadow || bCookie || bUber );

			int numRows    = bAdvanced ? lightTypes[i].constCount_advanced : lightTypes[i].constCount_simple;
			int memToAlloc = 4 * numRows;

			Assert( memToAlloc > 0 );

			lightDataCommon_t* pFlLightDataBlock = new lightDataCommon_t( memToAlloc );

			WriteLight( l, pFlLightDataBlock->pFlData, bShadow ? 1 : 0, bCookie ? 1 : 0 );

			if ( bShadow )
			{
				pCaller->DrawLightShadowView( view, 0, l );

				if ( l->IsSpot() )
				{
					if ( l->IsStatic() && l->HasValidAtlasSlot() )
					{
						QUEUE_FIRE( CommitTexture_ProjectedDepth, 0, GetShadowColorRT_Static( l->m_nCachedAtlasSlot ) );
					}
					else
					{
						QUEUE_FIRE( CommitTexture_ProjectedDepth, 0, GetShadowColorRT_Proj( 0 ) );
					}
				}
				else if ( l->IsPoint() )
				{
					if ( l->IsStatic() && l->HasValidAtlasSlot() )
					{
						QUEUE_FIRE( CommitTexture_DualParaboloidDepth, 0, GetShadowColorRT_StaticPoint( l->m_nCachedAtlasSlot ) );
					}
					else
					{
						QUEUE_FIRE( CommitTexture_DualParaboloidDepth, 0, GetShadowColorRT_DP( 0 ) );
					}
				}

				iDrawnShadowed++;
			}

			pFlLightDataBlock->numRows            = numRows;
			pFlLightDataBlock->numShadowedCookied = ( bShadow && bCookie ) ? 1 : 0;
			pFlLightDataBlock->numShadowed        = ( bShadow && !bCookie ) ? 1 : 0;
			pFlLightDataBlock->numCookied         = ( !bShadow && ( bCookie || bUber ) ) ? 1 : 0;
			pFlLightDataBlock->numSimple          = bAdvanced ? 0 : 1;
			QUEUE_FIRE( CommitLightData_Common, pFlLightDataBlock );

			if ( bCookie || ( !bShadow && bUber ) )
			{
				ITexture* pCookie = l->GetCookieForDraw();
				Assert( pCookie != NULL );
				QUEUE_FIRE( CommitTexture_Cookie, 0, pCookie );

				iDrawnCookied++;
			}

			if ( !bShadow && !bCookie )
				iDrawnSimple++;

			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->MatrixMode( MATERIAL_MODEL );
			pRenderContext->PushMatrix();
			pRenderContext->LoadMatrix( l->worldTransform );
			pRenderContext->Bind( lightTypes[i].pMatPassWorld );

			//ShaderStencilState_t stencilLightVolume;
			//set to reference 1 where draw succeeds
			//pRenderContext->Bind( lightTypes[i].pMatPassWorldStencilStage );
			//pMatPassWorldStencilStage only draws the backface where depth test fails
			//pRenderContext->SetStencilState( stencilLightVolume );
			//l->pMesh_World->Draw();
			//set to only draw where stencil is reference 1, set depth test to nearer
			//pRenderContext->SetStencilState( stencilLightVolume );

			l->pMesh_World->Draw();

			const bool bVolumetrics = l->HasVolumetrics() && bShadow && ( r_deferred_volumetric_intensity.GetFloat() > 0.0f );
			if ( bVolumetrics )
			{
				Assert( m_bDrawVolumetrics );
				Assert( l->pMesh_Volumetrics != NULL );

				volumeData_t data;
				data.iDataOffset    = 0;
				data.iSamplerOffset = 0;
				data.iNumRows       = lightTypes[i].constCount_advanced;
				data.bHasCookie     = bCookie;
#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
				data.iSamples = l->iVolumeSamples;
#endif
				QUEUE_FIRE( CommitVolumeData, data );

				DrawVolumePrepass( false, view, l );

				pRenderContext->PushRenderTargetAndViewport( pVolumBuffer0, 0, 0, view.width / 4, view.height / 4 );

				pRenderContext->Bind( lightTypes[i].pMatVolumeWorld );
				l->pMesh_Volumetrics->Draw();

				pRenderContext->PopRenderTargetAndViewport();

				iPassesVolumetrics[0]++;
			}

			pRenderContext->MatrixMode( MATERIAL_MODEL );
			pRenderContext->PopMatrix();
			pRenderContext.SafeRelease();
		}
		FOR_EACH_VEC_FAST_END
#endif
	}

	static bool s_bHadLightStats = false;
	if ( r_deferred_light_stats.GetBool() )
	{
		s_bHadLightStats = true;
		Assert( iNumLightTypes == 2 );

		const char* pszFilterName = "UNKNOWN FILTER";
		const char* pszProfile    = "DEPTH STENCIL (Hardware PCF)";

#if SHADOWMAPPING_USE_COLOR
		pszProfile = "COLOR RT (R32F)";
#endif

		switch ( SHADOWMAPPING_METHOD )
		{
		case SHADOWMAPPING_DEPTH_COLOR__RAW:
			pszFilterName = "RAW (R32F)";
			break;
		case SHADOWMAPPING_DEPTH_STENCIL__RAW:
			pszFilterName = "RAW (Hardware)";
			break;
		case SHADOWMAPPING_DEPTH_STENCIL__3X3_GAUSSIAN:
			pszFilterName = "3x3 GAUSS (Hardware PCF)";
			break;
		case SHADOWMAPPING_DEPTH_COLOR__4X4_SOFTWARE_BILINEAR_BOX:
			pszFilterName = "4x4 BOX (Shader Bilinear)";
			break;
		case SHADOWMAPPING_DEPTH_COLOR__4X4_SOFTWARE_BILINEAR_GAUSSIAN:
			pszFilterName = "4x4 GAUSS (Shader Bilinear)";
			break;
		case SHADOWMAPPING_DEPTH_COLOR__5X5_SOFTWARE_BILINEAR_GAUSSIAN:
			pszFilterName = "5x5 GAUSS (Shader Bilinear)";
			break;
		case SHADOWMAPPING_DEPTH_STENCIL__5X5_GAUSSIAN:
			pszFilterName = "5x5 GAUSS (Hardware PCF)";
			break;
		case SHADOWMAPPING_DEPTH_COLOR__PCSS_4X4_PCF_4X4:
			pszFilterName = "PCSS 4x4 + PCF 4x4";
			break;
		default:
			Assert(0); // add filter name to this switch case
		}

		extern int g_nShadowViewsSkippedLastFrame;
		extern int g_nShadowViewsRenderedLastFrame;
		const int nSlotsAssigned = GetStaticAtlasSlotCount();
		const int nPointSlotsAssigned = GetStaticPointAtlasSlotCount();

		engine->Con_NPrintf( 10, "=== DEFERRED LIGHTING ===" );
		engine->Con_NPrintf( 11, "Lights: %i in map, %i visible | Drawn: %i simple, %i shadow, %i cookie",
		                     m_hDeferredLights.Count(), m_hRenderLights.Count(),
		                     iDrawnSimple, iDrawnShadowed, iDrawnCookied );
		engine->Con_NPrintf( 12, "  Point - world: %i, fullscreen: %i | Spot - world: %i, fullscreen: %i",
		                     m_hPreSortedLights[LSORT_POINT_WORLD].Count(),
		                     m_hPreSortedLights[LSORT_POINT_FULLSCREEN].Count(),
		                     m_hPreSortedLights[LSORT_SPOT_WORLD].Count(),
		                     m_hPreSortedLights[LSORT_SPOT_FULLSCREEN].Count() );

		engine->Con_NPrintf( 13, "=== SHADOWING & CULLING ===" );
		engine->Con_NPrintf( 14, "Filter: %s - %s", pszProfile, pszFilterName );

		if ( r_deferred_csm_cull_convex.GetBool() )
		{
			const int nTotalTested = g_CSMCullStats.nTested[0] + g_CSMCullStats.nTested[1];
			const int nTotalCulled = g_CSMCullStats.nCulled[0] + g_CSMCullStats.nCulled[1];
			const float flPct = ( nTotalTested > 0 ) ? ( nTotalCulled * 100.0f / (float)nTotalTested ) : 0.0f;
			const float flPct0 = ( g_CSMCullStats.nTested[0] > 0 ) ? ( g_CSMCullStats.nCulled[0] * 100.0f / (float)g_CSMCullStats.nTested[0] ) : 0.0f;
			const float flPct1 = ( g_CSMCullStats.nTested[1] > 0 ) ? ( g_CSMCullStats.nCulled[1] * 100.0f / (float)g_CSMCullStats.nTested[1] ) : 0.0f;

			engine->Con_NPrintf( 15, "CSM Convex Culling: ON - Culled: %i / %i (%.1f%%)",
			                     nTotalCulled, nTotalTested, flPct );
			engine->Con_NPrintf( 16, "  Cascade 0: %i / %i (%.1f%%) | Cascade 1: %i / %i (%.1f%%)",
			                     g_CSMCullStats.nCulled[0], g_CSMCullStats.nTested[0], flPct0,
			                     g_CSMCullStats.nCulled[1], g_CSMCullStats.nTested[1], flPct1 );
		}
		else
		{
			engine->Con_NPrintf( 15, "CSM Convex Culling: OFF" );
			engine->Con_NPrintf( 16, "" );
		}

		engine->Con_NPrintf( 17, "Shadow Cache: %s | Views: %i cached (skipped), %i rendered",
		                     r_deferred_shadow_cache.GetBool() ? "ON" : "OFF",
		                     g_nShadowViewsSkippedLastFrame, g_nShadowViewsRenderedLastFrame );
		engine->Con_NPrintf( 18, "  Atlas slots: %i / %i (spot) | %i / %i (point)",
		                     nSlotsAssigned, MAX_STATIC_SHADOW_ATLAS_SLOTS,
		                     nPointSlotsAssigned, MAX_STATIC_POINT_SHADOW_SLOTS );

		engine->Con_NPrintf( 19, "=== RENDER PASSES ===" );
		engine->Con_NPrintf( 20, "Fullscreen passes - point: %i, spot: %i (total: %i)",
		                     iPassesDrawnFullscreen[0], iPassesDrawnFullscreen[1],
		                     iPassesDrawnFullscreen[0] + iPassesDrawnFullscreen[1] );
		engine->Con_NPrintf( 21, "Volumetric passes - world: %i, fullscreen: %i",
		                     iPassesVolumetrics[0], iPassesVolumetrics[1] );

		engine->Con_NPrintf( 22, "" );
		engine->Con_NPrintf( 23, "" );
		engine->Con_NPrintf( 24, "" );
	}
	else if ( s_bHadLightStats )
	{
		s_bHadLightStats = false;
		for ( int i = 10; i <= 24; ++i )
		{
			engine->Con_NPrintf( i, "" );
		}
	}
}

void CLightingManager::RenderVolumetrics( const CViewSetup& view )
{
	if ( !m_bDrawVolumetrics )
		return;

	const float flIntensity = r_deferred_volumetric_intensity.GetFloat();
	if ( flIntensity <= 0.0f )
		return;

	ITexture* pVolumBuffer0 = GetDefRT_VolumetricsBuffer( 0 );
	ITexture* pVolumBuffer1 = GetDefRT_VolumetricsBuffer( 1 );
	const int rtW = pVolumBuffer0 ? pVolumBuffer0->GetActualWidth() : ( view.width / 4 );
	const int rtH = pVolumBuffer0 ? pVolumBuffer0->GetActualHeight() : ( view.height / 4 );

	CMatRenderContextPtr pRenderContext( materials );

	const float flBlur = r_deferred_volumetric_blur.GetFloat();
	if ( flBlur > 0.0f )
	{
		IMaterial* pMatBlurX = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_BLUR_G6_X );
		IMaterial* pMatBlurY = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_BLUR_G6_Y );

		if ( pMatBlurX != NULL && pMatBlurY != NULL )
		{
			bool bFoundX = false, bFoundY = false;
			IMaterialVar* pBlurSizeX = pMatBlurX->FindVar( "$BLURSIZE", &bFoundX, true );
			IMaterialVar* pBlurSizeY = pMatBlurY->FindVar( "$BLURSIZE", &bFoundY, true );

			const int iPasses = Clamp( RoundFloatToInt( flBlur * 3.0f ), 2, 4 );
			static const float flPassScale[4] = { 1.5f, 3.5f, 7.0f, 12.0f };

			for ( int p = 0; p < iPasses; p++ )
			{
				const float flStep = flPassScale[p] * flBlur;
				if ( pBlurSizeX != NULL )
					pBlurSizeX->SetFloatValue( flStep );
				if ( pBlurSizeY != NULL )
					pBlurSizeY->SetFloatValue( flStep );

				pRenderContext->PushRenderTargetAndViewport( pVolumBuffer1, 0, 0, view.width / 4, view.height / 4 );
				pRenderContext->DrawScreenSpaceRectangle( pMatBlurX,
				                                          0, 0, view.width / 4, view.height / 4,
				                                          0, 0, view.width / 4 - 1, view.height / 4 - 1,
				                                          rtW, rtH );
				pRenderContext->PopRenderTargetAndViewport();

				pRenderContext->PushRenderTargetAndViewport( pVolumBuffer0, 0, 0, view.width / 4, view.height / 4 );
				pRenderContext->DrawScreenSpaceRectangle( pMatBlurY,
				                                          0, 0, view.width / 4, view.height / 4,
				                                          0, 0, view.width / 4 - 1, view.height / 4 - 1,
				                                          rtW, rtH );
				pRenderContext->PopRenderTargetAndViewport();
			}
		}
	}

	IMaterial* pMatBlend = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_BLEND );
	if ( pMatBlend != NULL )
	{
		bool bFound = false;
		IMaterialVar* pIntensityVar = pMatBlend->FindVar( "$INTENSITY", &bFound, true );
		if ( pIntensityVar != NULL )
		{
			pIntensityVar->SetFloatValue( flIntensity );
		}
	}

	pRenderContext->DrawScreenSpaceRectangle( pMatBlend,
	                                          0, 0, view.width, view.height,
	                                          0, 0, view.width / 4 - 1, view.height / 4 - 1,
	                                          rtW, rtH );
}

void CLightingManager::DoSceneDebug()
{
	if ( !r_deferred_light_stats.GetBool() )
		return;

#if DEBUG
	if ( r_deferred_light_stats.GetInt() >= 2 )
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->ClearBuffers( false, true );

		DebugLights_Draw_DebugMeshes();
	}
#endif


}

void CLightingManager::DebugLights_Draw_Boundingboxes()
{
	FOR_EACH_VEC_FAST( def_light_t*, m_hDeferredLights, l )
	{
		const bool bVisible = m_hRenderLights.HasElement( l );

		Color cFace = bVisible ? Color( 32, 196, 64, 16 ) : Color( 196, 64, 32, 16 );
		Color cEdge = Color( cFace[0] * 0.5f, cFace[1] * 0.5f, cFace[2] * 0.5f, 255.0f );

		debugoverlay->AddBoxOverlay2( vec3_origin,
		                              l->bounds_min,
		                              l->bounds_max,
		                              vec3_angle,
		                              cFace, cEdge, -1.0f );

		debugoverlay->AddBoxOverlay2( vec3_origin,
		                              l->bounds_min_naive,
		                              l->bounds_max_naive,
		                              vec3_angle,
		                              Color( 32, 64, 96, 16 ),
		                              Color( 32, 64, 96, 255 ), -1.0f );
	}
	FOR_EACH_VEC_FAST_END
}

#if DEBUG
void CLightingManager::DebugLights_Draw_DebugMeshes()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_WIREFRAME_DEBUG ) );

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();

	FOR_EACH_VEC_FAST( def_light_t*, m_hRenderLights, l )
	{
		if ( !l->pMesh_Debug )
			continue;

		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->LoadMatrix( l->worldTransform );

		l->pMesh_Debug->Draw();

		if ( l->pMesh_Debug_Volumetrics )
			l->pMesh_Debug_Volumetrics->Draw();
	}
	FOR_EACH_VEC_FAST_END

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
}
#endif
