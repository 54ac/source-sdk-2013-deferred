
#include "cbase.h"
#include "deferred/deferred_shared_common.h"

#include "tier0/memdbgon.h"

ConVar r_deferred_rt_shadowspot_res( "r_deferred_rt_shadowspot_res", "1024", FCVAR_RELOAD_TEXTURES | FCVAR_RELOAD_MATERIALS );
ConVar r_deferred_rt_shadowspot_static_res( "r_deferred_rt_shadowspot_static_res", "2048", FCVAR_RELOAD_TEXTURES | FCVAR_RELOAD_MATERIALS );
ConVar r_deferred_rt_shadowpoint_res( "r_deferred_rt_shadowpoint_res", "512", FCVAR_RELOAD_TEXTURES | FCVAR_RELOAD_MATERIALS );
ConVar r_deferred_shadow_filter_scale( "r_deferred_shadow_filter_scale", "1.25", FCVAR_ARCHIVE, "Shadow filter sample spread scale" );
ConVar r_deferred_point_light_half_lambert( "r_deferred_point_light_half_lambert", "1", FCVAR_ARCHIVE, "Point light diffuse wrap mode: 0 = pure Lambert, 1 = Half-Lambert" );

ConVar r_deferred_light_stats( "r_deferred_light_stats", "0", 0, "Shows stats panel for rendered lights, shadows, etc." );

ConVar r_deferred_light_global_override( "r_deferred_light_global_override", "0", 0, "Overrides light_deferred_global (or converted light_environment)" );
ConVar r_deferred_light_global_override_shadow( "r_deferred_light_global_override_shadow", "1", 0, "Render shadows from global light" );
ConVar r_deferred_light_global_override_diffuse( "r_deferred_light_global_override_diffuse", "1 1 1" );
ConVar r_deferred_light_global_override_ambient_high( "r_deferred_light_global_override_ambient_high", "0 0.04 0.08" );
ConVar r_deferred_light_global_override_ambient_low( "r_deferred_light_global_override_ambient_low", "0 0.07 0.09" );

ConVar r_deferred_csm_cull_convex( "r_deferred_csm_cull_convex", "1", FCVAR_ARCHIVE, "Convex hull caster culling for CSM" );
ConVar r_deferred_csm_cull_debug( "r_deferred_csm_cull_debug", "0", 0, "Visualize convex hull culling volumes" );

ConVar r_deferred_radiosity( "r_deferred_radiosity", "0", 0, "Enable radiosity for diffused lighting simulation - buggy" );
ConVar r_deferred_radiosity_propagate_count( "r_deferred_radiosity_propagate_count", "1" ); // 1
ConVar r_deferred_radiosity_propagate_far_count( "r_deferred_radiosity_propagate_far_count", "0" );
ConVar r_deferred_radiosity_blur_count( "r_deferred_radiosity_blur_count", "4" ); // 2
ConVar r_deferred_radiosity_blur_far_count( "r_deferred_radiosity_blur_far_count", "2" ); // 1
ConVar r_deferred_radiosity_debug( "r_deferred_radiosity_debug", "0", 0, "Shows radiosity nodes in the world for debugging" );

ConVar r_deferred_volumetric_intensity( "r_deferred_volumetric_intensity", "0.75", FCVAR_ARCHIVE, "Intensity multiplier for volumetric light" );
ConVar r_deferred_volumetric_blur( "r_deferred_volumetric_blur", "1.0", FCVAR_ARCHIVE, "Blur scale for volumetric lights" );

ConVar r_deferred_gbuffer_debug( "r_deferred_gbuffer_debug", "0", 0, "G-buffer normals + linear depth overlay" );
ConVar r_deferred_csm_debug( "r_deferred_csm_debug", "0", 0, "CSM cascade depth overlay" );

void OnCookieTableChanged( void *object, INetworkStringTable *stringTable, int stringNumber, const char *newString, void const *newData )
{
	if ( !newString || Q_strlen( newString ) < 1 )
		return;

	GetLightingManager()->OnCookieStringReceived( newString, stringNumber );
}

void CalcBoundaries( Vector *list, const int &num, Vector &min, Vector &max )
{
	Assert( num > 0 );

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
}
