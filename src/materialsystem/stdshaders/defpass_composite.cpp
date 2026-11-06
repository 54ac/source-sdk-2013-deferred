
#include "deferred_includes.h"

#include "composite_vs30.inc"
#include "composite_ps30.inc"

#include "tier0/memdbgon.h"

static CCommandBufferBuilder< CFixedCommandStorageBuffer< 512 > > tmpBuf;

ConVar building_cubemaps( "building_cubemaps", "0" );
static ConVar r_rimlight( "r_rimlight", "1", FCVAR_NONE );
static ConVar mat_fullbright( "mat_fullbright", "0", FCVAR_CHEAT );
static ConVar r_deferred_diffuse_ibl_scale( "r_deferred_diffuse_ibl_scale", "0.1", FCVAR_NONE, "Scale of diffuse ambient cubemap bounce" );

void InitParmsComposite( const defParms_composite &info, CBaseVSShader *pShader, IMaterialVar **params )
{
	if ( PARM_NO_DEFAULT( info.iAlphatestRef ) ||
		PARM_VALID( info.iAlphatestRef ) && PARM_FLOAT( info.iAlphatestRef ) == 0.0f )
		params[ info.iAlphatestRef ]->SetFloatValue( DEFAULT_ALPHATESTREF );

	PARM_INIT_INT( info.iPhong, 0 );
	PARM_INIT_FLOAT( info.iPhongBoost, 1.0f );
	PARM_INIT_FLOAT( info.iPhongScale, DEFAULT_PHONG_SCALE );
	PARM_INIT_INT( info.iPhongFresnel, 0 );
	PARM_INIT_VEC3( info.iPhongFresnelRanges, 0.05f, 0.5f, 1.0f );
	PARM_INIT_INT( info.iBaseMapAlphaPhongMask, 0 );
	PARM_INIT_INT( info.iInvertPhongMask, 0 );
	PARM_INIT_INT( info.iPhongAlbedoTint, 0 );

	PARM_INIT_FLOAT( info.iEnvmapContrast, 0.0f );
	PARM_INIT_FLOAT( info.iEnvmapSaturation, 1.0f );
	PARM_INIT_VEC3( info.iEnvmapTint, 1.0f, 1.0f, 1.0f );
	PARM_INIT_INT( info.iEnvmapFresnel, 0 );
	PARM_INIT_INT( info.iBaseAlphaEnvmapMask, 0 );
	PARM_INIT_INT( info.iNormalMapAlphaEnvmapMask, 0 );

	PARM_INIT_INT( info.iRimlightEnable, 0 );
	PARM_INIT_FLOAT( info.iRimlightExponent, 4.0f );
	PARM_INIT_FLOAT( info.iRimlightAlbedoScale, 0.0f );
	PARM_INIT_VEC3( info.iRimlightTint, 1.0f, 1.0f, 1.0f );
	PARM_INIT_INT( info.iRimlightModLight, 0 );

	PARM_INIT_VEC3( info.iSelfIllumTint, 1.0f, 1.0f, 1.0f );
	PARM_INIT_INT( info.iSelfIllumMaskInEnvmapAlpha, 0 );
	PARM_INIT_INT( info.iSelfIllumFresnelModulate, 0 );

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

void InitPassComposite( const defParms_composite &info, CBaseVSShader *pShader, IMaterialVar **params )
{
	if ( PARM_DEFINED( info.iAlbedo ) )
		pShader->LoadTexture( info.iAlbedo );

	if ( PARM_DEFINED( info.iEnvmap ) )
		pShader->LoadCubeMap( info.iEnvmap );

	if ( PARM_DEFINED( info.iEnvmapMask ) )
		pShader->LoadTexture( info.iEnvmapMask );

	if ( PARM_DEFINED( info.iAlbedo2 ) )
		pShader->LoadTexture( info.iAlbedo2 );

	if ( PARM_DEFINED( info.iBlendmodulate ) )
		pShader->LoadTexture( info.iBlendmodulate );

	if ( PARM_DEFINED( info.iSelfIllumMask ) )
		pShader->LoadTexture( info.iSelfIllumMask );

	if ( PARM_DEFINED( info.iBumpmap ) )
		pShader->LoadBumpMap( info.iBumpmap );
}

void DrawPassComposite( const defParms_composite &info, CBaseVSShader *pShader, IMaterialVar **params,
	IShaderShadow* pShaderShadow, IShaderDynamicAPI* pShaderAPI,
	VertexCompressionType_t vertexCompression, CDeferredPerMaterialContextData *pDeferredContext )
{
	const bool bModel = info.bModel;
	const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );
	const bool bVertexColor = !bModel && ( bIsDecal || IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR ) );
	const bool bFastVTex = g_pHardwareConfig->HasFastVertexTextures();

	const bool bAlbedo = PARM_TEX( info.iAlbedo );
	const bool bAlbedo2 = !bModel && bAlbedo && PARM_TEX( info.iAlbedo2 );

	const bool bAlphatest = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) && bAlbedo;
	const bool bTranslucent = ( IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT ) || bIsDecal ) && bAlbedo && !bAlphatest;

	const bool bNoCull = IS_FLAG_SET( MATERIAL_VAR_NOCULL );

	const bool bUseSRGB = true;

	const bool bUseSpecular = g_pConfig->UseSpecular();
	const bool bEnvmap = bUseSpecular && PARM_TEX( info.iEnvmap );
	const bool bEnvmapMask = bEnvmap && PARM_TEX( info.iEnvmapMask );
	const bool bEnvmapMask2 = bEnvmapMask && PARM_TEX( info.iEnvmapMask2 );
	const bool bEnvmapFresnel = bEnvmap && PARM_SET( info.iEnvmapFresnel );
	const bool bEnvmapCorrection = !info.bModel && bEnvmap && g_pConfig->UseParallaxMapping() && info.iEnvmapParallax != -1 && !params[info.iEnvmapParallax]->MatrixIsIdentity();

	const bool bPhong = bUseSpecular && g_pConfig->UsePhong() && PARM_SET( info.iPhong );
	const bool bBaseMapAlphaPhongMask = PARM_SET( info.iBaseMapAlphaPhongMask );
	const bool bNormalMapAlphaEnvmapMask = bEnvmap && ( PARM_SET( info.iNormalMapAlphaEnvmapMask ) || IS_FLAG_SET( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK ) );
	const bool bBaseAlphaEnvmapMask = bEnvmap && ( PARM_SET( info.iBaseAlphaEnvmapMask ) || IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK ) );
	const bool bBumpmap = g_pConfig->UseBumpmapping() && PARM_TEX( info.iBumpmap );
	const bool bBumpmask = bBumpmap && ( (bPhong && !bBaseMapAlphaPhongMask) || bNormalMapAlphaEnvmapMask );
	const bool bPhongFresnel = bPhong && ( PARM_SET( info.iPhongFresnel ) || PARM_DEFINED( info.iPhongFresnelRanges ) || PARM_DEFINED( info.iFresnelRanges ) );

	const bool bRimLight = r_rimlight.GetBool() && PARM_SET( info.iRimlightEnable );
	const bool bRimLightModLight = bRimLight && PARM_SET( info.iRimlightModLight );
	const bool bBlendmodulate = bAlbedo2 && PARM_TEX( info.iBlendmodulate );

	const bool bSelfIllum = !bAlbedo2 && IS_FLAG_SET( MATERIAL_VAR_SELFILLUM );
	const bool bSelfIllumMaskInEnvmapMask = bSelfIllum && bEnvmapMask && PARM_SET( info.iSelfIllumMaskInEnvmapAlpha );
	const bool bSelfIllumMask = bSelfIllum && !bSelfIllumMaskInEnvmapMask && !bEnvmapMask && PARM_TEX( info.iSelfIllumMask );

	const bool bNeedsFresnel = bPhongFresnel || bEnvmapFresnel;
	const bool bGBufferNormal = bPhong || bEnvmap || bRimLight || bNeedsFresnel;
	const bool bWorldEyeVec = bGBufferNormal;

	const int nTreeSwayMode = ( bModel && PARM_DEFINED( info.iTreeSway ) ) ? clamp( params[ info.iTreeSway ]->GetIntValue(), 0, 2 ) : 0;

	AssertMsgOnce( !(bTranslucent || bAlphatest) || !bAlbedo2,
		"blended albedo not supported by gbuffer pass!" );

	AssertMsgOnce( !PARM_TEX( info.iSelfIllumMask ) || !bEnvmapMask,
		"Can't use separate selfillum mask with envmap mask - use SELFILLUM_ENVMAPMASK_ALPHA instead." );

	SHADOW_STATE
	{
		pShaderShadow->SetDefaultState();
		pShaderShadow->EnableSRGBWrite( bUseSRGB );

		if ( bNoCull )
		{
			pShaderShadow->EnableCulling( false );
		}

		if ( bIsDecal )
		{
			pShaderShadow->EnablePolyOffset( SHADER_POLYOFFSET_DECAL );
		}

		int iVFmtFlags = VERTEX_POSITION;
		int iUserDataSize = 0;

		int *pTexCoordDim;
		int iTexCoordNum;
		GetTexcoordSettings( ( bModel && bIsDecal && bFastVTex ), iTexCoordNum, &pTexCoordDim );

		if ( bModel )
		{
			iVFmtFlags |= VERTEX_NORMAL;
			iVFmtFlags |= VERTEX_FORMAT_COMPRESSED;
		}
		else
		{
			if ( bAlbedo2 || bVertexColor )
				iVFmtFlags |= VERTEX_COLOR;
		}

		pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, bUseSRGB );

		if ( bGBufferNormal )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );
		}

		if ( bTranslucent )
		{
			pShader->EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
		}

		pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, false );

		pShaderShadow->EnableTexture( SHADER_SAMPLER9, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER9, false );

		if ( bEnvmap )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );

			if( g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE )
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, true );

			if ( bEnvmapMask )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );

				if ( bAlbedo2 )
					pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );
			}
		}
		else if ( bSelfIllumMask )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
		}

		if ( bAlbedo2 )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER5, bUseSRGB );

			if ( bBlendmodulate )
				pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
		}

		if ( bBumpmask )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER8, false );
		}

		pShaderShadow->EnableAlphaWrites( true );
		pShaderShadow->EnableDepthWrites( !bTranslucent );

		pShader->DefaultFog();

		pShaderShadow->VertexShaderVertexFormat( iVFmtFlags, iTexCoordNum, pTexCoordDim, iUserDataSize );

		DECLARE_STATIC_VERTEX_SHADER( composite_vs30 );
		SET_STATIC_VERTEX_SHADER_COMBO( MODEL, bModel );
		SET_STATIC_VERTEX_SHADER_COMBO( MORPHING_VTEX, bModel && !nTreeSwayMode && bFastVTex );
		SET_STATIC_VERTEX_SHADER_COMBO( DECAL, bModel && bIsDecal );
		SET_STATIC_VERTEX_SHADER_COMBO( EYEVEC, bWorldEyeVec );
		SET_STATIC_VERTEX_SHADER_COMBO( BASETEXTURE2, bAlbedo2 );
		SET_STATIC_VERTEX_SHADER_COMBO( BLENDMODULATE, bBlendmodulate );
		SET_STATIC_VERTEX_SHADER_COMBO( TREESWAY, nTreeSwayMode );
		SET_STATIC_VERTEX_SHADER_COMBO( VERTEXCOLOR, bVertexColor );
		SET_STATIC_VERTEX_SHADER( composite_vs30 );

		DECLARE_STATIC_PIXEL_SHADER( composite_ps30 );
		SET_STATIC_PIXEL_SHADER_COMBO( ALPHATEST, bAlphatest );
		SET_STATIC_PIXEL_SHADER_COMBO( TRANSLUCENT, bTranslucent );
		SET_STATIC_PIXEL_SHADER_COMBO( READNORMAL, bGBufferNormal );
		SET_STATIC_PIXEL_SHADER_COMBO( NOCULL, bNoCull );
		SET_STATIC_PIXEL_SHADER_COMBO( PHONG, bPhong );
		SET_STATIC_PIXEL_SHADER_COMBO( BUMPMASK, bBumpmask );
		SET_STATIC_PIXEL_SHADER_COMBO( ENVMAP, bEnvmap );
		SET_STATIC_PIXEL_SHADER_COMBO( ENVMAPMASK, bEnvmapMask );
		SET_STATIC_PIXEL_SHADER_COMBO( ENVMAPFRESNEL, bEnvmapFresnel );
		SET_STATIC_PIXEL_SHADER_COMBO( PHONGFRESNEL, bPhongFresnel );
		SET_STATIC_PIXEL_SHADER_COMBO( RIMLIGHT, bRimLight );
		SET_STATIC_PIXEL_SHADER_COMBO( RIMLIGHTMODULATELIGHT, bRimLightModLight );
		SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE2, bAlbedo2 );
		SET_STATIC_PIXEL_SHADER_COMBO( BLENDMODULATE, bBlendmodulate );
		SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM, bSelfIllum );
		SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM_MASK, bSelfIllumMask );
		SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM_ENVMAP_ALPHA, bSelfIllumMaskInEnvmapMask );
		SET_STATIC_PIXEL_SHADER_COMBO( PARALLAXCORRECT, bEnvmapCorrection );
		SET_STATIC_PIXEL_SHADER_COMBO( VERTEXCOLOR, bVertexColor );
		SET_STATIC_PIXEL_SHADER( composite_ps30 );
	}
	DYNAMIC_STATE
	{
		Assert( pDeferredContext != NULL );

		if ( pDeferredContext->m_bMaterialVarsChanged || !pDeferredContext->HasCommands( CDeferredPerMaterialContextData::DEFSTAGE_COMPOSITE )
			|| building_cubemaps.GetBool() )
		{
			tmpBuf.Reset();

			if ( bAlphatest )
			{
				PARM_VALIDATE( info.iAlphatestRef );
				tmpBuf.SetPixelShaderConstant1( 0, PARM_FLOAT( info.iAlphatestRef ) );
			}

			bool bLightingOnly = mat_fullbright.GetInt() == 2 && !IS_FLAG_SET( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
			if ( bAlbedo && !bLightingOnly )
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER0, info.iAlbedo );
			else
				tmpBuf.BindStandardTexture( SHADER_SAMPLER0, TEXTURE_GREY );

			if ( bBumpmask )
			{
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER8, info.iBumpmap );
			}

			if ( bEnvmap )
			{
				if ( building_cubemaps.GetBool() )
					tmpBuf.BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );
				else
				{
					if ( PARM_TEX( info.iEnvmap ) && !bModel )
						tmpBuf.BindTexture( pShader, SHADER_SAMPLER3, info.iEnvmap );
					else
						tmpBuf.BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );
				}

				if ( bEnvmapMask )
					tmpBuf.BindTexture( pShader, SHADER_SAMPLER4, info.iEnvmapMask );

				if ( bAlbedo2 )
				{
					if ( bEnvmapMask2 )
						tmpBuf.BindTexture( pShader, SHADER_SAMPLER7, info.iEnvmapMask2 );
					else
						tmpBuf.BindStandardTexture( SHADER_SAMPLER7, TEXTURE_WHITE );
				}

				tmpBuf.SetPixelShaderConstant( 5, info.iEnvmapTint );

				float fl6[4] = { 0 };
				fl6[0] = PARM_FLOAT( info.iEnvmapSaturation );
				fl6[1] = PARM_FLOAT( info.iEnvmapContrast );
				fl6[2] = bBaseAlphaEnvmapMask ? 1.0f : 0.0f;
				fl6[3] = bNormalMapAlphaEnvmapMask ? 1.0f : 0.0f;
				tmpBuf.SetPixelShaderConstant( 6, fl6 );
			}

			if ( bNeedsFresnel )
			{
				float vFresnel[4] = { 0.05f, 0.5f, 1.0f, 0.0f };
				int iFresnelParm = PARM_DEFINED( info.iPhongFresnelRanges ) ? info.iPhongFresnelRanges : info.iFresnelRanges;
				if ( PARM_DEFINED( iFresnelParm ) )
					params[iFresnelParm]->GetVecValue( vFresnel, 3 );
				float vFresnelEncoded[4] = {
					(vFresnel[1] - vFresnel[0]) * 2.0f,
					vFresnel[1],
					(vFresnel[2] - vFresnel[1]) * 2.0f,
					0.0f
				};
				tmpBuf.SetPixelShaderConstant( 7, vFresnelEncoded );
			}

			if ( bRimLight )
			{
				float fl9[4] = { 0 };
				fl9[0] = PARM_FLOAT( info.iRimlightExponent );
				fl9[1] = PARM_FLOAT( info.iRimlightAlbedoScale );
				tmpBuf.SetPixelShaderConstant( 9, fl9 );
			}

			if ( bAlbedo2 )
			{
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER5, info.iAlbedo2 );

				if ( bBlendmodulate )
				{
					tmpBuf.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, info.iBlendmodulateTransform );
					tmpBuf.BindTexture( pShader, SHADER_SAMPLER6, info.iBlendmodulate );
				}
			}

			if ( bSelfIllum && bSelfIllumMask )
			{
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER4, info.iSelfIllumMask );
			}

			tmpBuf.SetPixelShaderFogParams( 2 );

			float flPhongBoost = 1.0f;
			if ( PARM_DEFINED( info.iPhongBoost ) )
				flPhongBoost = PARM_FLOAT( info.iPhongBoost );
			else if ( PARM_DEFINED( info.iPhongScale ) )
				flPhongBoost = PARM_FLOAT( info.iPhongScale );

			float fl4[4] = {
				flPhongBoost,
				bBaseMapAlphaPhongMask ? 1.0f : 0.0f,
				PARM_SET( info.iInvertPhongMask ) ? 1.0f : 0.0f,
				PARM_SET( info.iPhongAlbedoTint ) ? 1.0f : 0.0f
			};
			tmpBuf.SetPixelShaderConstant( 4, fl4 );

			if ( bEnvmapCorrection )
			{
				tmpBuf.SetPixelShaderConstant( 11, params[info.iEnvmapOrigin]->GetVecValue() );
				tmpBuf.SetPixelShaderConstant( 12, params[info.iEnvmapParallax]->GetMatrixValue().Base(), 4 );
			}

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

			pDeferredContext->SetCommands( CDeferredPerMaterialContextData::DEFSTAGE_COMPOSITE, tmpBuf.Copy() );
		}

		pShaderAPI->SetDefaultState();

		if ( bModel && !nTreeSwayMode && bFastVTex )
			pShader->SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, VERTEX_SHADER_SHADER_SPECIFIC_CONST_11, SHADER_VERTEXTEXTURE_SAMPLER0 );

		DECLARE_DYNAMIC_VERTEX_SHADER( composite_vs30 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (bModel && (int)vertexCompression) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, (bModel && pShaderAPI->GetCurrentNumBones() > 0) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( MORPHING, (bModel && !nTreeSwayMode && pShaderAPI->IsHWMorphingEnabled()) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER( composite_vs30 );

		DECLARE_DYNAMIC_PIXEL_SHADER( composite_ps30 );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
		SET_DYNAMIC_PIXEL_SHADER( composite_ps30 );

		if ( bModel && !nTreeSwayMode && bFastVTex )
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

		pShaderAPI->ExecuteCommandBuffer( pDeferredContext->GetCommands( CDeferredPerMaterialContextData::DEFSTAGE_COMPOSITE ) );

		if ( bGBufferNormal )
			pShader->BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Normals() );

		pShader->BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_LightAccum() );
		pShader->BindTexture( SHADER_SAMPLER9, GetDeferredExt()->GetTexture_LightAccumSpecular() );

		if ( bEnvmap && bModel )
		{
			if ( building_cubemaps.GetBool() )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );
			else
				pShader->BindTexture( SHADER_SAMPLER3, info.iEnvmap );
		}

		CommitBaseDeferredConstants_Origin( pShaderAPI, 3 );

		if ( bWorldEyeVec )
		{
			float vEyepos[4] = {0,0,0,0};
			pShaderAPI->GetWorldSpaceCameraPosition( vEyepos );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, vEyepos );
		}

		if ( bRimLight )
		{
			pShaderAPI->SetPixelShaderConstant( 8, params[ info.iRimlightTint ]->GetVecValue() );
		}

		if ( bSelfIllum )
		{
			pShaderAPI->SetPixelShaderConstant( 10, params[ info.iSelfIllumTint ]->GetVecValue() );
		}

		const int iDeferredViewID = pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_VIEW_ID );
		ITexture *pLightAccum = ( iDeferredViewID != DEFERRED_VIEW_REFRACTION ) ? GetDeferredExt()->GetTexture_LightAccum() : NULL;
		int texW = pLightAccum ? pLightAccum->GetActualWidth() : 0;
		int texH = pLightAccum ? pLightAccum->GetActualHeight() : 0;
		if ( texW <= 0 || texH <= 0 )
		{
			ShaderViewport_t viewport;
			pShaderAPI->GetViewports( &viewport, 1 );
			texW = viewport.m_nWidth;
			texH = viewport.m_nHeight;
		}
		float fl1[4] = { texW > 0 ? ( 1.0f / (float)texW ) : 0.0f, texH > 0 ? ( 1.0f / (float)texH ) : 0.0f, r_deferred_diffuse_ibl_scale.GetFloat(), 0.0f };
		pShaderAPI->SetPixelShaderConstant( 1, fl1 );
	}

	pShader->Draw();
}