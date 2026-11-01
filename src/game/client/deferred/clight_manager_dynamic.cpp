#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/clight_manager.h"
#include "deferred/def_light_t.h"
#include "collisionutils.h"
#include "tier0/memdbgon.h"

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

void CLightingManager::FreeDynamicLight( def_light_t *&pLight, float flDecayTime )
{
	if ( pLight )
	{
		if ( flDecayTime > 0.0f )
		{
			def_light_temp_t *pTemp = new def_light_temp_t( flDecayTime );
			pTemp->pos = pLight->pos;
			pTemp->col_diffuse = pLight->col_diffuse;
			pTemp->flRadius = pLight->flRadius;
			pTemp->MakeDirtyAll();
			AddTempLight( pTemp );
		}

		RemoveLight( pLight );
		delete pLight;
		pLight = NULL;
	}
}
