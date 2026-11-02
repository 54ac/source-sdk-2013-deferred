#include "cbase.h"
#include "c_physicsprop.h"
#include "c_gib.h"
#include "c_baseviewmodel.h"
#include "c_prop_vehicle.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/clight_manager.h"
#include "collisionutils.h"
#include "physics_shared.h"
#include "tier0/memdbgon.h"

static bool s_bAtlasSlotUsed[ MAX_STATIC_SHADOW_ATLAS_SLOTS ];
static bool s_bAtlasSlotUsed_Point[ MAX_STATIC_POINT_SHADOW_SLOTS ];

static void OnDeferredShadowCacheChanged( IConVar *var, const char *pOldValue, float flOldValue );
ConVar r_deferred_shadow_cache( "r_deferred_shadow_cache", "1", FCVAR_ARCHIVE, "Static shadow map caching for stationary world lights", OnDeferredShadowCacheChanged );

static void OnDeferredShadowCacheChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	if ( GetLightingManager() )
	{
		GetLightingManager()->ReevaluateStaticAtlasSlots();
	}
}

void CLightingManager::ResetStaticAtlasSlots()
{
	Q_memset( s_bAtlasSlotUsed, 0, sizeof( s_bAtlasSlotUsed ) );
	Q_memset( s_bAtlasSlotUsed_Point, 0, sizeof( s_bAtlasSlotUsed_Point ) );
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

void CLightingManager::DirtyStaticLightsInBox( const Vector &vecCenter, float flRadius )
{
	if ( !r_deferred_shadow_cache.GetBool() )
		return;

	const Vector mins = vecCenter - Vector( flRadius, flRadius, flRadius );
	const Vector maxs = vecCenter + Vector( flRadius, flRadius, flRadius );

	for ( int i = 0; i < m_hDeferredLights.Count(); i++ )
	{
		def_light_t *l = m_hDeferredLights[i];
		if ( l && l->IsStatic() && ( l->IsSpot() || l->IsPoint() ) && l->HasValidAtlasSlot() && !l->IsShadowDirty() )
		{
			if ( DoesLightIntersectBox( l, vecCenter, flRadius, mins, maxs ) )
			{
				l->SetShadowDirty( true );
			}
		}
	}
}

void CLightingManager::OnEntityRemoved( C_BaseEntity *pEnt )
{
	if ( !r_deferred_shadow_cache.GetBool() || !pEnt )
		return;

	const int entIdx = pEnt->GetRefEHandle().GetEntryIndex();
	if ( entIdx >= 0 && entIdx < NUM_ENT_ENTRIES && m_pCachedPropStates && m_pCachedPropStates[entIdx].bActive )
	{
		DirtyStaticLightsInBox( m_pCachedPropStates[entIdx].vecCenter, m_pCachedPropStates[entIdx].flRadius );
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

	for ( C_BaseEntity *pEnt = cl_entitylist->FirstBaseEntity(); pEnt != NULL; pEnt = cl_entitylist->NextBaseEntity( pEnt ) )
	{
		const int entIdx = pEnt->GetRefEHandle().GetEntryIndex();
		if ( entIdx < 0 || entIdx >= NUM_ENT_ENTRIES )
			continue;

		CachedPropState_t &state = m_pCachedPropStates[entIdx];

		if ( pEnt->IsDormant() )
		{
			if ( state.bActive )
			{
				DirtyStaticLightsInBox( state.vecCenter, state.flRadius );
				state.bActive = false;
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

			const fltx4 curCX = ReplicateX4( propCenter.x );
			const fltx4 curCY = ReplicateX4( propCenter.y );
			const fltx4 curCZ = ReplicateX4( propCenter.z );
			const fltx4 curRad = ReplicateX4( flCurRadius );

			const fltx4 oldCX = ReplicateX4( oldCenter.x );
			const fltx4 oldCY = ReplicateX4( oldCenter.y );
			const fltx4 oldCZ = ReplicateX4( oldCenter.z );
			const fltx4 oldRad = ReplicateX4( flOldRad );

			const int nTotalLights = m_hDeferredLights.Count();
			int lIdx = 0;

			for ( ; lIdx + 3 < nTotalLights; lIdx += 4 )
			{
				def_light_t *l[4] = {
					m_hDeferredLights[lIdx],
					m_hDeferredLights[lIdx + 1],
					m_hDeferredLights[lIdx + 2],
					m_hDeferredLights[lIdx + 3]
				};

				int candidateMask[4] = {
					( l[0] && l[0]->IsStatic() && ( l[0]->IsSpot() || l[0]->IsPoint() ) && l[0]->HasValidAtlasSlot() && !( state.bActive && l[0]->IsShadowDirty() ) ) ? -1 : 0,
					( l[1] && l[1]->IsStatic() && ( l[1]->IsSpot() || l[1]->IsPoint() ) && l[1]->HasValidAtlasSlot() && !( state.bActive && l[1]->IsShadowDirty() ) ) ? -1 : 0,
					( l[2] && l[2]->IsStatic() && ( l[2]->IsSpot() || l[2]->IsPoint() ) && l[2]->HasValidAtlasSlot() && !( state.bActive && l[2]->IsShadowDirty() ) ) ? -1 : 0,
					( l[3] && l[3]->IsStatic() && ( l[3]->IsSpot() || l[3]->IsPoint() ) && l[3]->HasValidAtlasSlot() && !( state.bActive && l[3]->IsShadowDirty() ) ) ? -1 : 0
				};

				fltx4 mask = LoadUnalignedIntSIMD( candidateMask );
				if ( TestSignSIMD( mask ) == 0 )
					continue;

				float posX[4] = { l[0]->pos.x, l[1]->pos.x, l[2]->pos.x, l[3]->pos.x };
				float posY[4] = { l[0]->pos.y, l[1]->pos.y, l[2]->pos.y, l[3]->pos.y };
				float posZ[4] = { l[0]->pos.z, l[1]->pos.z, l[2]->pos.z, l[3]->pos.z };
				float radArr[4] = { l[0]->flRadius, l[1]->flRadius, l[2]->flRadius, l[3]->flRadius };

				fltx4 pX = LoadUnalignedSIMD( posX );
				fltx4 pY = LoadUnalignedSIMD( posY );
				fltx4 pZ = LoadUnalignedSIMD( posZ );
				fltx4 lRad = LoadUnalignedSIMD( radArr );

				fltx4 maxDist = AddSIMD( lRad, curRad );
				fltx4 maxDistSqr = MulSIMD( maxDist, maxDist );
				fltx4 dX = SubSIMD( curCX, pX );
				fltx4 dY = SubSIMD( curCY, pY );
				fltx4 dZ = SubSIMD( curCZ, pZ );
				fltx4 distSqr = AddSIMD( MulSIMD( dX, dX ), AddSIMD( MulSIMD( dY, dY ), MulSIMD( dZ, dZ ) ) );

				fltx4 inRange = CmpLeSIMD( distSqr, maxDistSqr );

				if ( state.bActive )
				{
					fltx4 oldMaxDist = AddSIMD( lRad, oldRad );
					fltx4 oldMaxDistSqr = MulSIMD( oldMaxDist, oldMaxDist );
					fltx4 odX = SubSIMD( oldCX, pX );
					fltx4 odY = SubSIMD( oldCY, pY );
					fltx4 odZ = SubSIMD( oldCZ, pZ );
					fltx4 oldDistSqr = AddSIMD( MulSIMD( odX, odX ), AddSIMD( MulSIMD( odY, odY ), MulSIMD( odZ, odZ ) ) );
					inRange = OrSIMD( inRange, CmpLeSIMD( oldDistSqr, oldMaxDistSqr ) );
				}

				fltx4 passMask = AndSIMD( mask, inRange );
				if ( TestSignSIMD( passMask ) == 0 )
					continue;

				for ( int j = 0; j < 4; ++j )
				{
					if ( ( SubInt( passMask, j ) & 0x80000000 ) != 0 )
					{
						def_light_t *curLight = l[j];
						bool bHit = DoesLightIntersectBox( curLight, propCenter, flCurRadius, mins, maxs );
						if ( !bHit && state.bActive )
						{
							if ( DoesLightIntersectBox( curLight, oldCenter, flOldRad, oldMins, oldMaxs ) )
							{
								bHit = true;
							}
						}
						if ( bHit )
						{
							curLight->SetShadowDirty( true );
						}
					}
				}
			}

			for ( ; lIdx < nTotalLights; ++lIdx )
			{
				def_light_t *curLight = m_hDeferredLights[lIdx];
				if ( !curLight || !curLight->IsStatic() || ( !curLight->IsSpot() && !curLight->IsPoint() ) || !curLight->HasValidAtlasSlot() )
					continue;

				if ( state.bActive && curLight->IsShadowDirty() )
					continue;

				bool bHit = DoesLightIntersectBox( curLight, propCenter, flCurRadius, mins, maxs );

				if ( !bHit && state.bActive )
				{
					if ( DoesLightIntersectBox( curLight, oldCenter, flOldRad, oldMins, oldMaxs ) )
					{
						bHit = true;
					}
				}

				if ( bHit )
				{
					curLight->SetShadowDirty( true );
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

