//====== Copyright  Sandern Corporation, All rights reserved. ===========//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "filesystem.h"
#include "utlbuffer.h"
#include "igamesystem.h"
#include "materialsystem_passthru.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "icommandline.h"

#include "deferred/deferred_shared_common.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


static int matCount = 0;
CON_COMMAND(print_num_replaced_mats, "")
{
	ConColorMsg( COLOR_GREEN, "%d replaced materials\n", matCount );
}

//-----------------------------------------------------------------------------
// List of materials that should be replaced
//-----------------------------------------------------------------------------
static const char * const pszShaderReplaceDict[][2] = {
	{ "vertexlitgeneric",				"DEFERRED_MODEL" },
	{ "lightmappedgeneric",			"DEFERRED_BRUSH" },
	{ "worldvertextransition",	"DEFERRED_BRUSH" },
	{ "decalmodulate",					"DEFERRED_DECALMODULATE" },
	{ "cable",									"DEFERRED_QUAD" },
};

static inline bool IsModelMaterial( const char *pMatName, const char *pTexGroup, bool bModelFlag )
{
	if ( bModelFlag )
		return true;

	if ( pTexGroup && !V_stricmp( pTexGroup, TEXTURE_GROUP_MODEL ) )
		return true;

	if ( pMatName && V_strnicmp( pMatName, "models/", 7 ) == 0 )
		return true;

	return false;
}

static inline bool ShouldReplaceUnlitGenericWithDeferredModel( const char *pMatName, const char *pTexGroup, bool bModelFlag, bool bAdditiveFlag )
{
	if ( bAdditiveFlag )
		return false;

	if ( pTexGroup && !V_stricmp( pTexGroup, TEXTURE_GROUP_VGUI ) )
		return false;

	return IsModelMaterial( pMatName, pTexGroup, bModelFlag );
}

static const char *GetDeferredReplacementShader( const char *pShaderName, const char *pMaterialName, const char *pTextureGroupName, bool bModel, bool bAdditive )
{
	if ( !pShaderName || !V_strnicmp( pShaderName, "DEFERRED_", 9 ) )
		return NULL;

	if ( pMaterialName && V_stristr( pMaterialName, "detailsprites" ) )
		return "DEFERRED_QUAD";

	if ( !V_stricmp( pShaderName, "unlitgeneric" ) )
	{
		if ( ShouldReplaceUnlitGenericWithDeferredModel( pMaterialName, pTextureGroupName, bModel, bAdditive ) )
			return "DEFERRED_MODEL";
		return NULL;
	}

	for ( const char* const* row : pszShaderReplaceDict )
	{
		if ( V_stristr( pShaderName, row[0] ) )
			return row[1];
	}

	return NULL;
}

// Copied from cdeferred_manager_client.cpp
static void ShaderReplaceReplMat( const char *szNewShadername, IMaterial *pMat )
{
	const char *pszOldShadername = pMat->GetShaderName();
	if ( pszOldShadername && !V_stricmp( pszOldShadername, szNewShadername ) )
		return;

	const char *pszMatname = pMat->GetName();

	KeyValuesAD msg( szNewShadername );

	const int nParams = pMat->ShaderParamCount();
	IMaterialVar **pParams = pMat->GetShaderParams();

	char str[ 512 ];

	for ( int i = 0; i < nParams; ++i )
	{
		IMaterialVar *pVar = pParams[ i ];
		const char *pVarName = pVar->GetName();

		if (!V_stricmp("$flags", pVarName) ||
			!V_stricmp("$flags_defined", pVarName) ||
			!V_stricmp("$flags2", pVarName) ||
			!V_stricmp("$flags_defined2", pVarName) )
			continue;

		switch ( pVar->GetType() )
		{
			case MATERIAL_VAR_TYPE_FLOAT:
				msg->SetFloat( pVarName, pVar->GetFloatValue() );
				break;

			case MATERIAL_VAR_TYPE_INT:
				msg->SetInt( pVarName, pVar->GetIntValue() );
				break;

			case MATERIAL_VAR_TYPE_STRING:
				msg->SetString( pVarName, pVar->GetStringValue() );
				break;

			case MATERIAL_VAR_TYPE_FOURCC:
				//Assert( 0 ); // JDTODO
				break;

			case MATERIAL_VAR_TYPE_VECTOR:
			{
				const float *pVal = pVar->GetVecValue();
				int dim = pVar->VectorSize();
				switch ( dim )
				{
				case 1:
					V_sprintf_safe( str, "[%f]", pVal[ 0 ] );
					break;
				case 2:
					V_sprintf_safe( str, "[%f %f]", pVal[ 0 ], pVal[ 1 ] );
					break;
				case 3:
					V_sprintf_safe( str, "[%f %f %f]", pVal[ 0 ], pVal[ 1 ], pVal[ 2 ] );
					break;
				case 4:
					V_sprintf_safe( str, "[%f %f %f %f]", pVal[ 0 ], pVal[ 1 ], pVal[ 2 ], pVal[ 3 ] );
					break;
				default:
					Assert( 0 );
					*str = 0;
				}
				msg->SetString( pVarName, str );
			}
				break;

			case MATERIAL_VAR_TYPE_MATRIX:
			{
				const float *pVal = pVar->GetMatrixValue().Base();
				V_sprintf_safe( str,
					"[%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f]",
					pVal[ 0 ],  pVal[ 1 ],  pVal[ 2 ],  pVal[ 3 ],
					pVal[ 4 ],  pVal[ 5 ],  pVal[ 6 ],  pVal[ 7 ],
					pVal[ 8 ],  pVal[ 9 ],  pVal[ 10 ], pVal[ 11 ],
					pVal[ 12 ], pVal[ 13 ], pVal[ 14 ], pVal[ 15 ] );
				msg->SetString( pVarName, str );
			}
			break;

			case MATERIAL_VAR_TYPE_TEXTURE:
			{
				const char *pTexName = pVar->GetTextureValue() ? pVar->GetTextureValue()->GetName() : pVar->GetStringValue();
				if ( pTexName && *pTexName )
				{
					msg->SetString( pVarName, pTexName );
				}
			}
			break;

			case MATERIAL_VAR_TYPE_MATERIAL:
				if ( pVar->GetMaterialValue() )
				{
					msg->SetString( pVarName, pVar->GetMaterialValue()->GetName() );
				}
				break;
		}
	}

	unsigned int nSavedFlags = 0;
	for ( int bit = 0; bit <= 30; ++bit )
	{
		MaterialVarFlags_t flag = static_cast<MaterialVarFlags_t>( 1 << bit );
		if ( pMat->GetMaterialVarFlag( flag ) )
		{
			nSavedFlags |= ( 1 << bit );
		}
	}

	const bool bIsQuad = ( V_stricmp( szNewShadername, "DEFERRED_QUAD" ) == 0 );

	const bool bAlphaBlending = !bIsQuad && ( pMat->IsTranslucent() || ( nSavedFlags & MATERIAL_VAR_TRANSLUCENT ) != 0 );
	const bool bAlphaTesting = bIsQuad || pMat->IsAlphaTested() || ( nSavedFlags & MATERIAL_VAR_ALPHATEST ) != 0;
	const bool bDecal = !bIsQuad && ( ( pszOldShadername != NULL && Q_stristr( pszOldShadername, "decal" ) != NULL ) ||
		( pszMatname != NULL && Q_stristr( pszMatname, "decal" ) != NULL ) ||
		( ( nSavedFlags & MATERIAL_VAR_DECAL ) != 0 ) );

	if ( bDecal )
	{
		nSavedFlags |= MATERIAL_VAR_DECAL;
		msg->SetInt( "$decal", 1 );
	}

	if ( bAlphaTesting )
	{
		nSavedFlags |= MATERIAL_VAR_ALPHATEST;
		nSavedFlags &= ~MATERIAL_VAR_TRANSLUCENT;
		msg->SetInt( "$alphatest", 1 );
		msg->SetInt( "$translucent", 0 );
	}
	else if ( bAlphaBlending || bDecal )
	{
		nSavedFlags |= MATERIAL_VAR_TRANSLUCENT;
		nSavedFlags &= ~MATERIAL_VAR_ALPHATEST;
		msg->SetInt( "$translucent", 1 );
		msg->SetInt( "$alphatest", 0 );
	}

	if ( bIsQuad || pMat->IsTwoSided() || ( nSavedFlags & MATERIAL_VAR_NOCULL ) != 0 )
	{
		nSavedFlags |= MATERIAL_VAR_NOCULL;
		msg->SetInt( "$nocull", 1 );
	}

	const bool bIsModel = ( V_stricmp( szNewShadername, "DEFERRED_MODEL" ) == 0 ) ||
	                      ( ( nSavedFlags & MATERIAL_VAR_MODEL ) != 0 ) ||
	                      ( pszMatname && V_stristr( pszMatname, "models/" ) );
	if ( bIsModel )
	{
		nSavedFlags |= MATERIAL_VAR_MODEL;
		msg->SetInt( "$model", 1 );
	}

	if ( !bIsQuad && bAlphaTesting && !msg->FindKey( "$alphatestreference" ) )
	{
		msg->SetFloat( "$alphatestreference", 0.5f );
	}

	if ( bIsQuad )
	{
		const bool bIsCable = ( pszOldShadername && V_stristr( pszOldShadername, "cable" ) ) ||
		                      ( pszMatname && V_stristr( pszMatname, "cable" ) );
		msg->SetInt( "$normalmode", bIsCable ? 1 : 0 );
		msg->SetFloat( "$alphatestreference", 0.4f );
		msg->SetInt( "$translucent", 0 );
		nSavedFlags &= ~MATERIAL_VAR_TRANSLUCENT;
		nSavedFlags &= ~MATERIAL_VAR_VERTEXALPHA;
		if ( bIsCable )
		{
			nSavedFlags |= MATERIAL_VAR_VERTEXCOLOR;
			msg->SetInt( "$vertexcolor", 1 );
		}
		else
		{
			nSavedFlags &= ~MATERIAL_VAR_VERTEXCOLOR;
			msg->SetInt( "$vertexcolor", 0 );
			msg->SetInt( "$vertexalpha", 0 );
		}
	}
	else
	{
		if ( ( nSavedFlags & MATERIAL_VAR_SELFILLUM ) != 0 )
		{
			msg->SetInt( "$selfillum", 1 );
		}

		if ( ( nSavedFlags & MATERIAL_VAR_BASEALPHAENVMAPMASK ) != 0 )
		{
			msg->SetInt( "$basealphaenvmapmask", 1 );
		}

		if ( ( nSavedFlags & MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK ) != 0 )
		{
			msg->SetInt( "$normalmapalphaenvmapmask", 1 );
		}

		if ( ( nSavedFlags & MATERIAL_VAR_VERTEXCOLOR ) != 0 )
		{
			msg->SetInt( "$vertexcolor", 1 );
		}

		if ( ( nSavedFlags & MATERIAL_VAR_VERTEXALPHA ) != 0 )
		{
			msg->SetInt( "$vertexalpha", 1 );
		}
	}

	pMat->SetShaderAndParams( msg );

	for ( int bit = 0; bit <= 30; ++bit )
	{
		MaterialVarFlags_t flag = static_cast<MaterialVarFlags_t>( 1 << bit );
		pMat->SetMaterialVarFlag( flag, ( nSavedFlags & ( 1 << bit ) ) != 0 );
	}

	pMat->RefreshPreservingMaterialVars();

	for ( int bit = 0; bit <= 30; ++bit )
	{
		MaterialVarFlags_t flag = static_cast<MaterialVarFlags_t>( 1 << bit );
		pMat->SetMaterialVarFlag( flag, ( nSavedFlags & ( 1 << bit ) ) != 0 );
	}

}

IMaterial* CDeferredMaterialSystem::FindProceduralMaterial( const char* pMaterialName, const char* pTextureGroupName,
                                                        KeyValues* pVMTKeyValues )
{
	const char* pShaderName = pVMTKeyValues ? pVMTKeyValues->GetName() : NULL;
	if ( pShaderName )
	{
		const bool bModel = ( pVMTKeyValues->GetInt( "$model", 0 ) != 0 );
		const bool bAdditive = ( pVMTKeyValues->GetInt( "$additive", 0 ) != 0 );
		const char *pReplShader = GetDeferredReplacementShader( pShaderName, pMaterialName, pTextureGroupName, bModel, bAdditive );
		if ( pReplShader )
		{
			pVMTKeyValues->SetName( pReplShader );
			matCount++;
		}
	}
	return BaseClass::FindProceduralMaterial( pMaterialName, pTextureGroupName, pVMTKeyValues );
}

IMaterial* CDeferredMaterialSystem::CreateMaterial( const char* pMaterialName, KeyValues* pVMTKeyValues )
{
	const char* pShaderName = pVMTKeyValues ? pVMTKeyValues->GetName() : NULL;
	if ( pShaderName )
	{
		const bool bModel = ( pVMTKeyValues->GetInt( "$model", 0 ) != 0 );
		const bool bAdditive = ( pVMTKeyValues->GetInt( "$additive", 0 ) != 0 );
		const char *pReplShader = GetDeferredReplacementShader( pShaderName, pMaterialName, NULL, bModel, bAdditive );
		if ( pReplShader )
		{
			pVMTKeyValues->SetName( pReplShader );
			matCount++;
		}
	}
	return BaseClass::CreateMaterial( pMaterialName, pVMTKeyValues );
}

IMaterial* CDeferredMaterialSystem::ReplaceMaterialInternal( IMaterial* pMat ) const
{
	if ( !pMat || pMat->IsErrorMaterial() )
		return pMat;

	if ( V_stristr( pMat->GetName(), "_subrect" ) )
		return pMat;

	const char* pMatName = pMat->GetName();
	const char* pShaderName = pMat->GetShaderName();
	if ( pShaderName )
	{
		const bool bModel = pMat->GetMaterialVarFlag( MATERIAL_VAR_MODEL );
		const bool bAdditive = pMat->GetMaterialVarFlag( MATERIAL_VAR_ADDITIVE );
		const char *pTexGroup = pMat->GetTextureGroupName();
		const char *pReplShader = GetDeferredReplacementShader( pShaderName, pMatName, pTexGroup, bModel, bAdditive );
		if ( pReplShader )
		{
			ShaderReplaceReplMat( pReplShader, pMat );
			matCount++;
		}
	}
	return pMat;
}
