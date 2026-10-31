
#include "cbase.h"
#include "deferred/deferred_shared_common.h"

#include "tier0/memdbgon.h"

ConVar r_deferred_rt_shadowspot_res( "r_deferred_rt_shadowspot_res", "1024", FCVAR_RELOAD_TEXTURES | FCVAR_RELOAD_MATERIALS );
#if DEFCFG_ADAPTIVE_SHADOWMAP_LOD
ConVar r_deferred_rt_shadowspot_lod1_res( "r_deferred_rt_shadowspot_lod1_res", "512", FCVAR_RELOAD_TEXTURES | FCVAR_RELOAD_MATERIALS );
ConVar r_deferred_rt_shadowspot_lod2_res( "r_deferred_rt_shadowspot_lod2_res", "256", FCVAR_RELOAD_TEXTURES | FCVAR_RELOAD_MATERIALS );
#endif
ConVar r_deferred_rt_shadowpoint_res( "r_deferred_rt_shadowpoint_res", "512", FCVAR_RELOAD_TEXTURES | FCVAR_RELOAD_MATERIALS );
ConVar r_deferred_shadow_filter_scale( "r_deferred_shadow_filter_scale", "1.25", FCVAR_ARCHIVE, "Shadow filter sample spread scale" );
ConVar r_deferred_point_light_half_lambert( "r_deferred_point_light_half_lambert", "1", FCVAR_ARCHIVE, "Point light diffuse wrap mode: 0 = pure Lambert, 1 = Half-Lambert" );
ConVar r_deferred_point_shadow_mode( "r_deferred_point_shadow_mode", "1", FCVAR_ARCHIVE | FCVAR_RELOAD_TEXTURES | FCVAR_RELOAD_MATERIALS, "Point light shadow mode: 0 = dual-paraboloid, 1 = tetrahedral" );
#if DEFCFG_ADAPTIVE_SHADOWMAP_LOD
ConVar r_deferred_rt_shadowpoint_lod1_res( "r_deferred_rt_shadowpoint_lod1_res", "512", FCVAR_RELOAD_TEXTURES | FCVAR_RELOAD_MATERIALS );
ConVar r_deferred_rt_shadowpoint_lod2_res( "r_deferred_rt_shadowpoint_lod2_res", "256", FCVAR_RELOAD_TEXTURES | FCVAR_RELOAD_MATERIALS );
#endif

ConVar r_deferred_light_stats( "r_deferred_light_stats", "0", 0, "Shows stats panel for rendered lights, shadows, etc." );

ConVar r_deferred_light_global_override( "r_deferred_light_global_override", "0", 0, "Overrides light_deferred_global (or converted light_environment)" );
ConVar r_deferred_light_global_override_shadow( "r_deferred_light_global_override_shadow", "1", 0, "Render shadows from global light" );
ConVar r_deferred_light_global_override_diffuse( "r_deferred_light_global_override_diffuse", "1 1 1" );
ConVar r_deferred_light_global_override_ambient_high( "r_deferred_light_global_override_ambient_high", "0 0.04 0.08" );
ConVar r_deferred_light_global_override_ambient_low( "r_deferred_light_global_override_ambient_low", "0 0.07 0.09" );

ConVar r_deferred_csm_max_dist( "r_deferred_csm_max_dist", "4096", FCVAR_ARCHIVE, "Maximum view distance for directional cascaded shadow maps" );
ConVar r_deferred_csm_split_dist( "r_deferred_csm_split_dist", "1024", FCVAR_ARCHIVE, "Split distance between cascades" );

ConVar r_deferred_radiosity( "r_deferred_radiosity", "0", 0, "Enable radiosity for diffused lighting simulation - buggy" );
ConVar r_deferred_radiosity_propagate_count( "r_deferred_radiosity_propagate_count", "1" ); // 1
ConVar r_deferred_radiosity_propagate_far_count( "r_deferred_radiosity_propagate_far_count", "0" );
ConVar r_deferred_radiosity_blur_count( "r_deferred_radiosity_blur_count", "4" ); // 2
ConVar r_deferred_radiosity_blur_far_count( "r_deferred_radiosity_blur_far_count", "2" ); // 1
ConVar r_deferred_radiosity_nodes( "r_deferred_radiosity_nodes", "0", 0, "Shows radiosity nodes in the world for debugging" );

ConVar r_deferred_volumetric_intensity( "r_deferred_volumetric_intensity", "0.75", FCVAR_ARCHIVE, "Intensity multiplier for volumetric light" );
ConVar r_deferred_volumetric_blur( "r_deferred_volumetric_blur", "1.0", FCVAR_ARCHIVE, "Blur scale for volumetric lights" );

void OnCookieTableChanged( void *object, INetworkStringTable *stringTable, int stringNumber, const char *newString, void const *newData )
{
	if ( !newString || Q_strlen( newString ) < 1 )
		return;

	GetLightingManager()->OnCookieStringReceived( newString, stringNumber );
}

void CalcBoundaries( Vector *list, const int &num, Vector &min, Vector &max )
{
	Assert( num > 0 );

#if DEFCFG_USE_SSE && 0
	fltx4 vTestPoint = _mm_set_ps( list[0].x, list[0].y, list[0].z, 0 );
	fltx4 vMin = vTestPoint;
	fltx4 vMax = vTestPoint;

	for( int i = 1; i < num; i++ )
	{
		vTestPoint = _mm_set_ps( list[i].x, list[i].y, list[i].z, 0 );
		vMin = _mm_min_ps( vMin, vTestPoint );
		vMax = _mm_max_ps( vMax, vTestPoint );
	}

	min = Vector( SubFloat( vMin, 0 ), SubFloat( vMin, 1 ), SubFloat( vMin, 2 ) );
	max = Vector( SubFloat( vMax, 0 ), SubFloat( vMax, 1 ), SubFloat( vMax, 2 ) );
#else
	min = *list;
	max = *list;

	for ( int i = 1; i < num; i++ )
	{
		for ( int x = 0; x < 3; x++ )
		{
			min[ x ] = Min( min[ x ], list[ i ][ x ] );
			max[ x ] = Max( max[ x ], list[ i ][ x ] );
		}
	}
#endif
}