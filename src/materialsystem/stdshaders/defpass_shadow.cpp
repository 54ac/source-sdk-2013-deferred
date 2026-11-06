
#include "deferred_includes.h"

#include "shadowpass_vs30.inc"
#include "shadowpass_ps30.inc"

#include "tier0/memdbgon.h"

static CCommandBufferBuilder< CFixedCommandStorageBuffer< 512 > > tmpBuf;

void InitParmsShadowPass( const defParms_shadow &info, CBaseVSShader *pShader, IMaterialVar **params )
{
	if ( PARM_NO_DEFAULT( info.iAlphatestRef ) ||
		PARM_VALID( info.iAlphatestRef ) && PARM_FLOAT( info.iAlphatestRef ) == 0.0f )
		params[ info.iAlphatestRef ]->SetFloatValue( DEFAULT_ALPHATESTREF );

	PARM_INIT_INT( info.iTreeSway, 0 );
	PARM_INIT_FLOAT( info.iTreeSwayHeight, 1000.0f );
	PARM_INIT_FLOAT( info.iTreeSwayStartHeight, 0.2f );
	PARM_INIT_FLOAT( info.iTreeSwayRadius, 300.0f );
	PARM_INIT_FLOAT( info.iTreeSwayStartRadius, 0.1f );
	PARM_INIT_FLOAT( info.iTreeSwaySpeed, 1.0f );
	PARM_INIT_FLOAT( info.iTreeSwaySpeedHighWindMultiplier, 2.0f );
	PARM_INIT_FLOAT( info.iTreeSwayStrength, 10.0f );
	PARM_INIT_FLOAT( info.iTreeSwayScrumbleSpeed, 0.1f );
	PARM_INIT_FLOAT( info.iTreeSwayScrumbleStrength, 0.1f );
	PARM_INIT_FLOAT( info.iTreeSwayScrumbleFrequency, 0.1f );
	PARM_INIT_FLOAT( info.iTreeSwayFalloffExp, 1.5f );
	PARM_INIT_FLOAT( info.iTreeSwayScrumbleFalloffExp, 1.0f );
	PARM_INIT_FLOAT( info.iTreeSwaySpeedLerpStart, 3.0f );
	PARM_INIT_FLOAT( info.iTreeSwaySpeedLerpEnd, 6.0f );
	PARM_INIT_INT( info.iTreeSwayStatic, 0 );
}

void InitPassShadowPass( const defParms_shadow &info, CBaseVSShader *pShader, IMaterialVar **params )
{
	if ( PARM_DEFINED( info.iAlbedo ) )
		pShader->LoadTexture( info.iAlbedo );

	if ( PARM_DEFINED( info.iAlbedo2 ) )
		pShader->LoadTexture( info.iAlbedo2 );
}

void DrawPassShadowPass( const defParms_shadow &info, CBaseVSShader *pShader, IMaterialVar **params,
	IShaderShadow* pShaderShadow, IShaderDynamicAPI* pShaderAPI,
	VertexCompressionType_t vertexCompression, CDeferredPerMaterialContextData *pDeferredContext )
{
	const bool bModel = info.bModel;
	const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );
	const bool bFastVTex = g_pHardwareConfig->HasFastVertexTextures();
	const bool bNoCull = IS_FLAG_SET( MATERIAL_VAR_NOCULL );

	const bool bAlbedo = PARM_TEX( info.iAlbedo );
	const bool bAlbedo2 = PARM_TEX( info.iAlbedo2 );
	const bool bAlphatest = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) && bAlbedo;

	const int nTreeSwayMode = ( bModel && PARM_DEFINED( info.iTreeSway ) ) ? clamp( params[ info.iTreeSway ]->GetIntValue(), 0, 2 ) : 0;

	SHADOW_STATE
	{
		pShaderShadow->SetDefaultState();

		pShaderShadow->EnableSRGBWrite( false );

		if ( bNoCull )
		{
			pShaderShadow->EnableCulling( false );
		}

		int iVFmtFlags = VERTEX_POSITION | VERTEX_NORMAL;
		int iUserDataSize = 0;

		int *pTexCoordDim;
		int iTexCoordNum;
		GetTexcoordSettings( ( bModel && bIsDecal && bFastVTex ),
							 iTexCoordNum, &pTexCoordDim );

		if ( bModel )
		{
			iVFmtFlags |= VERTEX_FORMAT_COMPRESSED;
		}

#if !DEFCFG_ENABLE_RADIOSITY
		if ( bAlphatest )
#endif
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );

			if ( bAlbedo2 )
				pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
		}

		pShaderShadow->VertexShaderVertexFormat( iVFmtFlags, iTexCoordNum, pTexCoordDim, iUserDataSize );

		DECLARE_STATIC_VERTEX_SHADER( shadowpass_vs30 );
		SET_STATIC_VERTEX_SHADER_COMBO( MODEL, bModel );
		SET_STATIC_VERTEX_SHADER_COMBO( MORPHING_VTEX, bModel && !nTreeSwayMode && bFastVTex );
		SET_STATIC_VERTEX_SHADER_COMBO( MULTITEXTURE, bAlbedo2 );
		SET_STATIC_VERTEX_SHADER_COMBO( TREESWAY, nTreeSwayMode );
		SET_STATIC_VERTEX_SHADER( shadowpass_vs30 );

		DECLARE_STATIC_PIXEL_SHADER( shadowpass_ps30 );
		SET_STATIC_PIXEL_SHADER_COMBO( ALPHATEST, bAlphatest );
		SET_STATIC_PIXEL_SHADER_COMBO( MULTITEXTURE, bAlbedo2 );
		SET_STATIC_PIXEL_SHADER( shadowpass_ps30 );
	}
	DYNAMIC_STATE
	{
		Assert( pDeferredContext != NULL );
		const int shadowMode = pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_MODE );
		const int radiosityOutput = ( DEFCFG_ENABLE_RADIOSITY && pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_RADIOSITY ) );

		if ( pDeferredContext->m_bMaterialVarsChanged || !pDeferredContext->HasCommands( CDeferredPerMaterialContextData::DEFSTAGE_SHADOW ) )
		{
			tmpBuf.Reset();

			if ( bAlphatest )
			{
				PARM_VALIDATE( info.iAlphatestRef );

				tmpBuf.SetPixelShaderConstant1( 0, PARM_FLOAT( info.iAlphatestRef ) );
#if !DEFCFG_ENABLE_RADIOSITY
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER0, info.iAlbedo );
#endif
			}

#if DEFCFG_ENABLE_RADIOSITY
			if ( bAlbedo )
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER0, info.iAlbedo );
			else
				tmpBuf.BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );

			if ( bAlbedo2 )
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER1, info.iAlbedo2 );
#endif

			if ( nTreeSwayMode > 0 )
			{
				float flParams[4];

				flParams[0] = PARM_DEFINED( info.iTreeSwayScrumbleFalloffExp ) ? PARM_FLOAT( info.iTreeSwayScrumbleFalloffExp ) : 1.0f;
				flParams[1] = PARM_DEFINED( info.iTreeSwayFalloffExp ) ? PARM_FLOAT( info.iTreeSwayFalloffExp ) : 1.0f;
				flParams[2] = PARM_DEFINED( info.iTreeSwayScrumbleSpeed ) ? PARM_FLOAT( info.iTreeSwayScrumbleSpeed ) : 3.0f;
				flParams[3] = PARM_DEFINED( info.iTreeSwaySpeedHighWindMultiplier ) ? PARM_FLOAT( info.iTreeSwaySpeedHighWindMultiplier ) : 2.0f;
				tmpBuf.SetVertexShaderConstant( 17, flParams );

				flParams[0] = PARM_DEFINED( info.iTreeSwayHeight ) ? PARM_FLOAT( info.iTreeSwayHeight ) : 1000.0f;
				flParams[1] = PARM_DEFINED( info.iTreeSwayStartHeight ) ? PARM_FLOAT( info.iTreeSwayStartHeight ) : 0.1f;
				flParams[2] = PARM_DEFINED( info.iTreeSwayRadius ) ? PARM_FLOAT( info.iTreeSwayRadius ) : 300.0f;
				flParams[3] = PARM_DEFINED( info.iTreeSwayStartRadius ) ? PARM_FLOAT( info.iTreeSwayStartRadius ) : 0.2f;
				tmpBuf.SetVertexShaderConstant( 18, flParams );

				flParams[0] = PARM_DEFINED( info.iTreeSwaySpeed ) ? PARM_FLOAT( info.iTreeSwaySpeed ) : 1.0f;
				flParams[1] = PARM_DEFINED( info.iTreeSwayStrength ) ? PARM_FLOAT( info.iTreeSwayStrength ) : 10.0f;
				flParams[2] = PARM_DEFINED( info.iTreeSwayScrumbleFrequency ) ? PARM_FLOAT( info.iTreeSwayScrumbleFrequency ) : 12.0f;
				flParams[3] = PARM_DEFINED( info.iTreeSwayScrumbleStrength ) ? PARM_FLOAT( info.iTreeSwayScrumbleStrength ) : 10.0f;
				tmpBuf.SetVertexShaderConstant( 19, flParams );

				flParams[0] = PARM_DEFINED( info.iTreeSwaySpeedLerpStart ) ? PARM_FLOAT( info.iTreeSwaySpeedLerpStart ) : 3.0f;
				flParams[1] = PARM_DEFINED( info.iTreeSwaySpeedLerpEnd ) ? PARM_FLOAT( info.iTreeSwaySpeedLerpEnd ) : 6.0f;
				flParams[2] = 0.0f;
				flParams[3] = 0.0f;
				tmpBuf.SetVertexShaderConstant( 20, flParams );
			}

			tmpBuf.End();

			pDeferredContext->SetCommands( CDeferredPerMaterialContextData::DEFSTAGE_SHADOW, tmpBuf.Copy() );
		}

		pShaderAPI->SetDefaultState();

		if ( bModel && !nTreeSwayMode && bFastVTex )
			pShader->SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, VERTEX_SHADER_SHADER_SPECIFIC_CONST_11, SHADER_VERTEXTEXTURE_SAMPLER0 );

		DECLARE_DYNAMIC_VERTEX_SHADER( shadowpass_vs30 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (bModel && (int)vertexCompression) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, (bModel && pShaderAPI->GetCurrentNumBones() > 0) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( MORPHING, (bModel && !nTreeSwayMode && pShaderAPI->IsHWMorphingEnabled()) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( SHADOW_MODE, shadowMode );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( RADIOSITY, radiosityOutput );
		SET_DYNAMIC_VERTEX_SHADER( shadowpass_vs30 );

		DECLARE_DYNAMIC_PIXEL_SHADER( shadowpass_ps30 );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( SHADOW_MODE, shadowMode );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( RADIOSITY, radiosityOutput );
		SET_DYNAMIC_PIXEL_SHADER( shadowpass_ps30 );

		if ( bModel && bFastVTex )
		{
			bool bUnusedTexCoords[3] = { false, true, !pShaderAPI->IsHWMorphingEnabled() || !bIsDecal };
			pShaderAPI->MarkUnusedVertexFields( 0, 3, bUnusedTexCoords );
		}

		if ( nTreeSwayMode > 0 )
		{
			float flParams[4];
			flParams[0] = pShaderAPI->CurrentTime();
			Vector windDir = ( PARM_DEFINED( info.iTreeSwayStatic ) && PARM_SET( info.iTreeSwayStatic ) )
				? Vector( 0.5f, 0.5f, 0.0f )
				: pShaderAPI->GetVectorRenderingParameter( VECTOR_RENDERPARM_WIND_DIRECTION );
			flParams[1] = windDir.x;
			flParams[2] = windDir.y;
			flParams[3] = 0.0f;
			pShaderAPI->SetVertexShaderConstant( 16, flParams );
		}

		pShaderAPI->ExecuteCommandBuffer( pDeferredContext->GetCommands( CDeferredPerMaterialContextData::DEFSTAGE_SHADOW ) );

		switch ( shadowMode )
		{
		case DEFERRED_SHADOW_MODE_ORTHO:
			{
				CommitShadowcastingConstants_Ortho( pShaderAPI,
					pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX ),
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, VERTEX_SHADER_SHADER_SPECIFIC_CONST_1,
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_2
					);
			}
			break;
		case DEFERRED_SHADOW_MODE_PROJECTED:
		case DEFERRED_SHADOW_MODE_TETRA:
			{
				CommitShadowcastingConstants_Proj( pShaderAPI,
					pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX ),
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, VERTEX_SHADER_SHADER_SPECIFIC_CONST_1,
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_2
					);
			}
			break;
		}

#ifdef SHADOWMAPPING_USE_COLOR
		CommitViewVertexShader( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_7 );
#endif
	}

	pShader->Draw();
}