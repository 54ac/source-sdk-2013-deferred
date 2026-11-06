
#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "mapentities.h"
#include "filesystem.h"
#include "bspfile.h"
#include "utlbuffer.h"
#include "lzmaDecoder.h"
#include "icommandline.h"

#include "tier0/memdbgon.h"

static CDeferredManagerServer __g_defmanager;

IGameSystem* DeferredManagerSystem()
{
	return &__g_defmanager;
}

CDeferredManagerServer *GetDeferredManager()
{
	return &__g_defmanager;
}

CDeferredManagerServer::CDeferredManagerServer()
{
}

CDeferredManagerServer::~CDeferredManagerServer()
{
}

bool CDeferredManagerServer::Init()
{
	return true;
}

void CDeferredManagerServer::Shutdown()
{
}

#define LIGHT_MIN_LIGHT_VALUE 0.08f
#define LIGHT_RADIUS_SCALE 1.25f

float ComputeLightRadius( float radius, float intensity, float constant_attn, float linear_attn, float quadratic_attn )
{
	float flLightRadius = radius;
	if (flLightRadius == 0.0f)
	{
		const float ratio = constant_attn + 100.0f * linear_attn + 10000.0f * quadratic_attn;
		const float effectiveRatio = ( ratio > 0.0f ) ? ratio : 1.0f;

		if (quadratic_attn == 0.0f)
		{
			if (linear_attn == 0.0f)
			{
				flLightRadius = 2000.0f;
			}
			else
			{
				flLightRadius = ((effectiveRatio / LIGHT_MIN_LIGHT_VALUE - constant_attn) / linear_attn) * LIGHT_RADIUS_SCALE;
				if (flLightRadius < 0.0f)
					flLightRadius = 0.0f;
			}
		}
		else
		{
			const float a = quadratic_attn;
			const float b = linear_attn;
			const float c = constant_attn - effectiveRatio / LIGHT_MIN_LIGHT_VALUE;
			const float discrim = b * b - 4.0f * a * c;
			if (discrim < 0.0f)
			{
				flLightRadius = 2000.0f;
			}
			else
			{
				flLightRadius = (-b + sqrtf(discrim)) / (2.0f * a);
				if (flLightRadius < 0.0f)
					flLightRadius = 0.0f;
				else
					flLightRadius *= LIGHT_RADIUS_SCALE;
			}
		}
	}
	return clamp( flLightRadius, 128.0f, 4096.0f );
}

float ComputeLightRadius( const dworldlight_t &light )
{
	float flLightRadius = light.radius;
	if (flLightRadius == 0.0f)
	{
		const float ratio = light.constant_attn + 100.0f * light.linear_attn + 10000.0f * light.quadratic_attn;
		const float effectiveRatio = ( ratio > 0.0f ) ? ratio : 1.0f;

		if ( light.quadratic_attn == 0.0f )
		{
			if ( light.linear_attn == 0.0f )
			{
				flLightRadius = 2000.0f;
			}
			else
			{
				flLightRadius = ((effectiveRatio / LIGHT_MIN_LIGHT_VALUE - light.constant_attn) / light.linear_attn) * LIGHT_RADIUS_SCALE;
				if (flLightRadius < 0.0f)
					flLightRadius = 0.0f;
			}
		}
		else
		{
			const float a = light.quadratic_attn;
			const float b = light.linear_attn;
			const float c = light.constant_attn - effectiveRatio / LIGHT_MIN_LIGHT_VALUE;
			const float discrim = b * b - 4.0f * a * c;
			if (discrim < 0.0f)
			{
				flLightRadius = 2000.0f;
			}
			else
			{
				flLightRadius = (-b + sqrtf(discrim)) / (2.0f * a);
				if (flLightRadius < 0.0f)
					flLightRadius = 0.0f;
				else
					flLightRadius *= LIGHT_RADIUS_SCALE;
			}
		}
	}

	return clamp( flLightRadius, 128.0f, 4096.0f );
}

static float CalibrateFalloffPower( float flRadius, float flFiftyDist, float flZeroDist, float c, float l, float q )
{
	if ( flFiftyDist > 0.0f )
	{
		if ( flZeroDist <= flFiftyDist )
			flZeroDist = 2.0f * flFiftyDist;

		const float flRatio = 1.0f - ( flFiftyDist / flZeroDist );
		if ( flRatio > 0.001f && flRatio < 0.999f )
		{
			const float p = -0.69314718f / logf( flRatio );
			return clamp( p, 0.5f, 4.0f );
		}
		return 1.0f;
	}

	if ( flRadius > 0.0f )
	{
		const float dHalf = flRadius * 0.5f;
		const float denomHalf = c + l * dHalf + q * dHalf * dHalf;

		if ( denomHalf > 0.00001f )
		{
			if ( c > 0.0001f )
			{
				const float normAtten = c / denomHalf;
				if ( normAtten > 0.0001f && normAtten < 0.9999f )
				{
					const float p = -logf( normAtten ) / 0.69314718f;
					return clamp( p, 0.5f, 4.0f );
				}
				else if ( normAtten >= 0.9999f )
				{
					return 0.5f;
				}
				else
				{
					return 4.0f;
				}
			}
			else
			{
				const float wl = l * dHalf;
				const float wq = q * dHalf * dHalf;
				if ( ( wl + wq ) > 0.00001f )
				{
					const float p = ( wl * 1.0f + wq * 2.0f ) / ( wl + wq );
					return clamp( p, 0.5f, 4.0f );
				}
				else if ( q > 0.0f )
				{
					return 2.0f;
				}
			}
		}
	}

	return 1.0f;
}

ConVar r_deferred_autoenvlight_ambient_intensity_low("r_deferred_autoenvlight_ambient_intensity_low", "0.1");
ConVar r_deferred_autoenvlight_ambient_intensity_high("r_deferred_autoenvlight_ambient_intensity_high", "0.2");
ConVar r_deferred_autoenvlight_diffuse_intensity("r_deferred_autoenvlight_diffuse_intensity", "0.7");

void CDeferredManagerServer::LevelInitPreEntity()
{
	if ( gpGlobals->eLoadType == MapLoad_LoadGame )
		return;


	const char* entStr = engine->GetMapEntitiesString();

	if ( V_stristr( entStr, "light_deferred_global" ) )
		return;

	const FileHandle_t hFile = g_pFullFileSystem->Open(MapName(), "rb");
	if ( !hFile )
		return;

	dheader_t header;
	g_pFullFileSystem->Read( &header, sizeof(dheader_t), hFile );

	const lump_t &lightLump = header.lumps[LUMP_WORLDLIGHTS];
	dworldlight_t* lights;
	size_t lightCount;

	if ( lightLump.uncompressedSize )
	{
		byte* compressed = new byte[lightLump.filelen];
		g_pFullFileSystem->Seek( hFile, lightLump.fileofs, FILESYSTEM_SEEK_HEAD );
		g_pFullFileSystem->Read( compressed, lightLump.filelen, hFile );

		if ( CLZMA::IsCompressed( compressed ) )
		{
			if ( lightLump.uncompressedSize % sizeof( dworldlight_t ) )
			{
				delete[] compressed;
				g_pFullFileSystem->Close( hFile );
				return;
			}

			lightCount = lightLump.uncompressedSize / sizeof( dworldlight_t );
			lights = new dworldlight_t[lightCount];
			CLZMA::Uncompress( compressed, reinterpret_cast<byte*>( lights ) );
		}
		else
		{
			delete[] compressed;
			g_pFullFileSystem->Close( hFile );
			Error( "Compressed lump that isn't compressed?" );
			return;
		}

		delete[] compressed;
	}
	else
	{
		if ( lightLump.filelen % sizeof( dworldlight_t ) )
		{
			g_pFullFileSystem->Close( hFile );
			return;
		}

		g_pFullFileSystem->Seek( hFile, lightLump.fileofs, FILESYSTEM_SEEK_HEAD );

		lightCount = lightLump.filelen / sizeof( dworldlight_t );

		lights = new dworldlight_t[lightCount];
		g_pFullFileSystem->Read( lights, lightLump.filelen, hFile );
	}

	g_pFullFileSystem->Close( hFile );

	KeyValuesAD vmfFile( "vmf" );

	char szTokenBuffer[MAPKEY_MAXLENGTH];
	for ( ; true; entStr = MapEntity_SkipToNextEntity( entStr, szTokenBuffer ) )
	{
		char token[MAPKEY_MAXLENGTH];
		entStr = MapEntity_ParseToken( entStr, token );

		if ( !entStr )
			break;

		if ( token[0] != '{' )
		{
			Error( "MapEntity_ParseAllEntities: found %s when expecting {", token );
			continue;
		}

		CEntityMapData entData( (char*)entStr );

		char className[MAPKEY_MAXLENGTH];
		entData.ExtractValue( "classname", className );
		int iType;
		if ( FStrEq( className, "light" ) )
			iType = 0;
		else if ( FStrEq( className, "light_spot" ) )
			iType = 1;
		else if ( FStrEq( className, "point_spotlight" ) )
			iType = 2;
		else if ( FStrEq( className, "light_environment" ) )
			iType = 3;
		else
			continue;

		char keyName[MAPKEY_MAXLENGTH];
		char value[MAPKEY_MAXLENGTH];
		KeyValues* pSubKey = new KeyValues( className );
		if ( entData.GetFirstKey( keyName, value ) )
		{
			do
			{
				pSubKey->SetString( keyName, value );
			}
			while ( entData.GetNextKey( keyName, value ) );
		}
		pSubKey->SetInt( "light_type", iType );
		vmfFile->AddSubKey( pSubKey );
	}

	KeyValuesDumpAsDevMsg( vmfFile, 0, 4 );

	struct SpotLightPair_t
	{
		KeyValues* spotlight;
		KeyValues* light;
	};

	CUtlVector<SpotLightPair_t> spotLightPairs;

	// Find light_spot and point_spotlight entities at same place
	FOR_EACH_TRUE_SUBKEY( vmfFile, entity1 )
	{
		const int type1 = entity1->GetInt( "light_type" );
		if ( type1 != 1 )
			continue;
		bool bSkip = false;
		const int numPairs = spotLightPairs.Count();
		for ( int i = 0; i < numPairs; ++i )
		{
			const SpotLightPair_t& pair = spotLightPairs[i];
			if (pair.light == entity1)
			{
				bSkip = true;
				break;
			}
		}
		if ( bSkip )
			continue;

		Vector pos1;
		QAngle rot1;
		UTIL_StringToVector( pos1.Base(), entity1->GetString( "origin" ) );
		UTIL_StringToVector( rot1.Base(), entity1->GetString( "angles" ) );

		FOR_EACH_TRUE_SUBKEY( vmfFile, entity2 )
		{
			const int type2 = entity2->GetInt( "light_type" );
			if ( type2 != 2 )
				continue;

			if (entity1 == entity2)
				continue;

			bool bSkip2 = false;
			const int numPairs2 = spotLightPairs.Count();
			for ( int i = 0; i < numPairs2; ++i )
			{
				const SpotLightPair_t& pair = spotLightPairs[i];
				if (pair.spotlight == entity2)
				{
					bSkip2 = true;
					break;
				}
			}
			if (bSkip2)
				continue;


			Vector pos2;
			QAngle rot2;
			UTIL_StringToVector( pos2.Base(), entity2->GetString( "origin" ) );
			UTIL_StringToVector( rot2.Base(), entity2->GetString( "angles" ) );

			if ( CloseEnough( pos1.x, pos2.x, 8.f ) && CloseEnough( pos1.y, pos2.y, 8.f ) && CloseEnough( pos1.z, pos2.z, 32.f ) && CloseEnough( rot1.y, rot2.y, 10.f ) )
			{
				DevMsg(1, "Found matching lights at positions %f %f %f and %f %f %f of types %d and %d\n", pos1.x, pos1.y, pos1.z, pos2.x, pos2.y, pos2.z, type1, type2 );
				SpotLightPair_t& pair = spotLightPairs[spotLightPairs.AddToTail()];
				pair.light = type1 == 1 ? entity1 : entity2;
				pair.spotlight = type1 != 1 ? entity1 : entity2;
				break;
			}
		}
	}

#define COPY_LIGHT_DATA( name ) pair.spotlight->SetString( name, pair.light->GetString( name ) )

	const int numPairs = spotLightPairs.Count();
	for ( int i = 0; i < numPairs; ++i )
	{
		SpotLightPair_t& pair = spotLightPairs[i];
		vmfFile->RemoveSubKey( pair.light );
		COPY_LIGHT_DATA( "_light" );
		COPY_LIGHT_DATA( "_exponent" );
		COPY_LIGHT_DATA( "_cone" );
		COPY_LIGHT_DATA( "_inner_cone" );
		COPY_LIGHT_DATA( "_constant_attn" );
		COPY_LIGHT_DATA( "_linear_attn" );
		COPY_LIGHT_DATA( "_quadratic_attn" );
		COPY_LIGHT_DATA( "_fifty_percent_distance" );
		COPY_LIGHT_DATA( "_zero_percent_distance" );
		COPY_LIGHT_DATA( "_distance" );
		COPY_LIGHT_DATA( "shadow_filter_scale" );
		pair.spotlight->SetInt( "light_type", 5 );
		pair.light->deleteThis();
	}

	const char* szParamDiffuse = GetLightParamName( LPARAM_DIFFUSE );
	const char* szParamLightType = GetLightParamName( LPARAM_LIGHTTYPE );
	const char* szParamSpotConeInner = GetLightParamName( LPARAM_SPOTCONE_INNER );
	const char* szParamSpotConeOuter = GetLightParamName( LPARAM_SPOTCONE_OUTER );
	const char* szParamPower = GetLightParamName( LPARAM_POWER );
	const char* szParamRadius = GetLightParamName( LPARAM_RADIUS );
	const char* szParamVisDist = GetLightParamName( LPARAM_VIS_DIST );
	const char* szParamVisRange = GetLightParamName( LPARAM_VIS_RANGE );
	const char* szParamShadowDist = GetLightParamName( LPARAM_SHADOW_DIST );
	const char* szParamShadowRange = GetLightParamName( LPARAM_SHADOW_RANGE );
	const char* szParamShadowFilterScale = GetLightParamName( LPARAM_SHADOW_FILTER_SCALE );
	const char* szParamVolumeSamples = GetLightParamName( LPARAM_VOLUME_SAMPLES );

	bool bCreatedGlobalLight = false;
	//CUtlVector<const dworldlight_t*> unspawnedLights;
	FOR_EACH_TRUE_SUBKEY( vmfFile, entity )
	{
		const int type = entity->GetInt( "light_type" );
		if ( type == -1 || ( type == 3 && bCreatedGlobalLight ))
			continue;

		Vector pos;
		QAngle rot;
		UTIL_StringToVector( pos.Base(), entity->GetString( "origin" ) );
		if ( KeyValues* angle = entity->FindKey( "angles" ) ) {
			UTIL_StringToVector( rot.Base(), angle->GetString() );
			if ((type == 1 || type == 5) && entity->GetFloat("pitch") != 0.0f)
				rot.x = -entity->GetFloat("pitch");

		}
		else
			rot = vec3_angle;

		if ( type == 3 )
		{
			rot.x = -entity->GetInt( "pitch" );
			CDeferredLightGlobal* lightEntity = static_cast<CDeferredLightGlobal*>( CBaseEntity::CreateNoSpawn( "light_deferred_global", pos, rot ) );
			if ( !lightEntity )
				break;

			int color[4], ambient[4];
			UTIL_StringToIntArray( color, 4, entity->GetString( "_light" ) );
			UTIL_StringToIntArray( ambient, 4, entity->GetString( "_ambient" ) );

			const float ds = r_deferred_autoenvlight_diffuse_intensity.GetFloat();
			const float asl = r_deferred_autoenvlight_ambient_intensity_low.GetFloat();
			const float ash = r_deferred_autoenvlight_ambient_intensity_high.GetFloat();

			lightEntity->KeyValue( "diffuse", UTIL_VarArgs("%d %d %d %f", color[0], color[1], color[2], color[3] * ds ) );
			lightEntity->KeyValue( "ambient_high", UTIL_VarArgs("%d %d %d %f", ambient[0], ambient[1], ambient[2], ambient[3] * ash ) );
			lightEntity->KeyValue( "ambient_low", UTIL_VarArgs("%d %d %d %f", ambient[0], ambient[1], ambient[2], ambient[3] * asl ) );
			lightEntity->KeyValue( "spawnflags", "3" );

			DispatchSpawn( lightEntity );

			bCreatedGlobalLight = true;
			continue;
		}

		for ( uint i = 0; i < lightCount; ++i )
		{
			const dworldlight_t& light = lights[i];
			if ( light.type != emit_spotlight && light.type != emit_point )
				continue;

			if ( CloseEnough( light.origin, pos, 4.f ) )
			{
				if ( light.type == emit_point )
				{
					if ( !entity->FindKey( "_constant_attn" ) )
						entity->SetFloat( "_constant_attn", light.constant_attn );
					if ( !entity->FindKey( "_linear_attn" ) )
						entity->SetFloat( "_linear_attn", light.linear_attn );
					if ( !entity->FindKey( "_quadratic_attn" ) )
						entity->SetFloat( "_quadratic_attn", light.quadratic_attn );

					entity->SetFloat( "_distance", ComputeLightRadius( light ) );
					break;
				}

				Vector fwdEnt;
				AngleVectors( rot, &fwdEnt );
				if ( DotProduct( fwdEnt, light.normal ) > 0.95f )
				{
					VectorAngles( light.normal, rot );
					entity->SetFloat( "_inner_cone", acos( clamp( light.stopdot, -1.f, 1.f ) ) * 180.f / M_PI_F );
					entity->SetFloat( "_cone", acos( clamp( light.stopdot2, -1.f, 1.f ) ) * 180.f / M_PI_F );

					if ( !entity->FindKey( "_constant_attn" ) )
						entity->SetFloat( "_constant_attn", light.constant_attn );
					if ( !entity->FindKey( "_linear_attn" ) )
						entity->SetFloat( "_linear_attn", light.linear_attn );
					if ( !entity->FindKey( "_quadratic_attn" ) )
						entity->SetFloat( "_quadratic_attn", light.quadratic_attn );

					entity->SetFloat( "_distance", ComputeLightRadius( light ) );
					break;
				}
			}
		}

		//out:
		CDeferredLight* lightEntity = static_cast<CDeferredLight*>( CBaseEntity::CreateNoSpawn( "light_deferred", pos, rot ) );
		if ( !lightEntity )
			break;

		int color[4] = { 255, 255, 255, 255 };
		UTIL_StringToIntArray( color, 4, entity->GetString( type == 2 ? "rendercolor" : "_light" ) );
		if ( type == 5 || ( type != 2 && color[3] > 255 ) )
			color[3] = 255;
		if ( color[3] <= 0 )
			color[3] = 255;

		char string[256];
		V_sprintf_safe( string, "%d %d %d %d", color[0], color[1], color[2], color[3] );
		lightEntity->KeyValue( szParamDiffuse, string );

		const float flFiftyDist = entity->GetFloat( "_fifty_percent_distance", 0.0f );
		float flZeroDist = entity->GetFloat( "_zero_percent_distance", 0.0f );
		float radius = 0.0f;

		if ( type == 2 )
		{
			radius = entity->GetFloat( "spotlightlength" );
		}
		else if ( flFiftyDist > 0.0f )
		{
			if ( flZeroDist <= flFiftyDist )
				flZeroDist = 2.0f * flFiftyDist;

			radius = flZeroDist;
		}
		else
		{
			radius = ComputeLightRadius( entity->GetFloat( "_distance" ), color[3], entity->GetFloat( "_constant_attn" ), entity->GetFloat( "_linear_attn" ), entity->GetFloat( "_quadratic_attn" ) );
		}

		float flPower = 1.0f;
		if ( type != 2 )
		{
			flPower = CalibrateFalloffPower(
				radius,
				flFiftyDist,
				flZeroDist,
				entity->GetFloat( "_constant_attn" ),
				entity->GetFloat( "_linear_attn" ),
				entity->GetFloat( "_quadratic_attn" )
			);
		}

		lightEntity->KeyValue( "spawnflags", type == 2 || type == 5 ? "11" : "3" );
		if ( type == 1 || type == 5 )
		{
			float flOuterCone = entity->GetFloat( "_cone" ) * 2.0f;
			float flInnerCone = entity->GetFloat( "_inner_cone" ) * 2.0f;
			if ( flOuterCone <= 0.0f )
				flOuterCone = 90.0f;
			flOuterCone = clamp( flOuterCone, 1.0f, 135.0f );
			if ( flInnerCone <= 0.0f || flInnerCone > flOuterCone )
				flInnerCone = flOuterCone;

			lightEntity->KeyValue( szParamLightType, "1" );
			lightEntity->KeyValue( szParamSpotConeInner, flInnerCone );
			lightEntity->KeyValue( szParamSpotConeOuter, flOuterCone );
			lightEntity->KeyValue( szParamPower, flPower );
			float flFilterScale = entity->GetFloat( "shadow_filter_scale" );
			if ( flFilterScale > 0.0f )
				lightEntity->KeyValue( szParamShadowFilterScale, flFilterScale );
			if ( type == 5 )
				lightEntity->KeyValue( szParamVolumeSamples, 50 );
		}
		else if ( type == 2 )
		{
			lightEntity->KeyValue( szParamLightType, "1" );
			const float width = entity->GetFloat( "spotlightwidth" );
			lightEntity->KeyValue( szParamSpotConeInner, width );
			lightEntity->KeyValue( szParamSpotConeOuter, width * 1.5f );
			lightEntity->KeyValue( szParamPower, 1.0f );
			lightEntity->KeyValue( szParamVolumeSamples, 50 );
		}
		else
		{
			lightEntity->KeyValue( szParamLightType, "0" );
			lightEntity->KeyValue( szParamPower, flPower );
		}

		lightEntity->KeyValue( szParamRadius, radius );
		lightEntity->KeyValue( szParamVisDist, max( 4096.0f, radius * 3.0f ) );
		lightEntity->KeyValue( szParamVisRange, 1024.0f );
		lightEntity->KeyValue( szParamShadowDist, max( 4096.0f, radius * 3.0f ) );
		lightEntity->KeyValue( szParamShadowRange, 1024.0f );

		if ( KeyValues* targetname = entity->FindKey( "targetname" ) )
			lightEntity->KeyValue( "targetname", targetname->GetString() );

		if ( KeyValues* parent = entity->FindKey( "parentname" ) )
			lightEntity->KeyValue( "parentname", parent->GetString() );

		DispatchSpawn( lightEntity );
	}

	delete[] lights;
}

int CDeferredManagerServer::AddCookieTexture( const char *pszCookie )
{
	Assert( g_pStringTable_LightCookies != NULL );

	return  g_pStringTable_LightCookies->AddString( true, pszCookie );
}

void CDeferredManagerServer::AddWorldLight( CDeferredLight *l )
{
	CDeferredLightContainer *pC = FindAvailableContainer();

	if ( !pC )
	{
		pC = assert_cast< CDeferredLightContainer* >( CreateEntityByName( "deferred_light_container" ) );
		DispatchSpawn( pC );
	}

	pC->AddWorldLight( l );
}
