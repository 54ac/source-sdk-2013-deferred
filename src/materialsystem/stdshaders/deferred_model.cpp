
#include "deferred_includes.h"

#include "tier0/memdbgon.h"

// DEFINE_FALLBACK_SHADER( VertexLitGeneric, DEFERRED_MODEL )
// DEFINE_FALLBACK_SHADER( UnlitGeneric, DEFERRED_MODEL )

BEGIN_VS_SHADER( DEFERRED_MODEL, "" )
	BEGIN_SHADER_PARAMS

		SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "", "" )

		SHADER_PARAM( LITFACE, SHADER_PARAM_TYPE_BOOL, "0", "" )

		SHADER_PARAM( PHONG, SHADER_PARAM_TYPE_BOOL, "0", "enables phong specular" )
		SHADER_PARAM( PHONGBOOST, SHADER_PARAM_TYPE_FLOAT, "1.0", "phong specular boost" )
		SHADER_PARAM( PHONGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "20.0", "phong exponent" )
		SHADER_PARAM( PHONGEXPONENTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "phong exponent texture" )
		SHADER_PARAM( PHONGFRESNELRANGES, SHADER_PARAM_TYPE_VEC3, "[0.05 0.5 1]", "phong fresnel ranges" )
		SHADER_PARAM( BASEMAPALPHAPHONGMASK, SHADER_PARAM_TYPE_BOOL, "0", "use basemap alpha for phong mask" )
		SHADER_PARAM( INVERTPHONGMASK, SHADER_PARAM_TYPE_BOOL, "0", "invert phong mask" )
		SHADER_PARAM( PHONGALBEDOTINT, SHADER_PARAM_TYPE_BOOL, "0", "tint phong with albedo" )

		SHADER_PARAM( PHONG_SCALE, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( PHONG_EXP, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( PHONG_MAP, SHADER_PARAM_TYPE_TEXTURE, "", "" )
		SHADER_PARAM( PHONG_FRESNEL, SHADER_PARAM_TYPE_BOOL, "", "" )

		SHADER_PARAM( FRESNELRANGES, SHADER_PARAM_TYPE_VEC3, "", "" )
		SHADER_PARAM( ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0.5", "" )

		SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "shadertest/shadertest_env", "envmap" )
		SHADER_PARAM( ENVMAPTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "envmap tint" )
		SHADER_PARAM( ENVMAPCONTRAST, SHADER_PARAM_TYPE_FLOAT, "0.0", "contrast 0 == normal 1 == color*color" )
		SHADER_PARAM( ENVMAPSATURATION, SHADER_PARAM_TYPE_FLOAT, "1.0", "saturation 0 == greyscale 1 == normal" )
		SHADER_PARAM( ENVMAPFRESNEL, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( ENVMAPMASK, SHADER_PARAM_TYPE_TEXTURE, "shadertest/shadertest_envmask", "envmap mask" )
		SHADER_PARAM( BASEALPHAENVMAPMASK, SHADER_PARAM_TYPE_BOOL, "0", "indicates that the alpha channel of the base texture is an env map mask" )
		SHADER_PARAM( NORMALMAPALPHAENVMAPMASK, SHADER_PARAM_TYPE_BOOL, "0", "indicates that the alpha channel of the normal map is an env map mask" )

		SHADER_PARAM( RIMLIGHT, SHADER_PARAM_TYPE_BOOL, "0", "enables rim lighting" )
		SHADER_PARAM( RIMLIGHTEXPONENT, SHADER_PARAM_TYPE_FLOAT, "4.0", "Exponent for rim lights" )
		SHADER_PARAM( RIMLIGHTALBEDOSCALE, SHADER_PARAM_TYPE_FLOAT, "0.0", "Albedo influence on rim light" )
		SHADER_PARAM( RIMLIGHTTINT, SHADER_PARAM_TYPE_VEC3, "[1 1 1]", "Tint for rim lights" )
		SHADER_PARAM( RIMLIGHT_MODULATE_BY_LIGHT, SHADER_PARAM_TYPE_BOOL, "0", "Modulate rimlight by lighting." )

		SHADER_PARAM( SELFILLUMTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Self-illumination tint" )
		SHADER_PARAM( SELFILLUM_ENVMAPMASK_ALPHA, SHADER_PARAM_TYPE_BOOL, "0", "defines that self illum value comes from env map mask alpha" )
		SHADER_PARAM( SELFILLUMFRESNEL, SHADER_PARAM_TYPE_BOOL, "0", "Self illum fresnel" )
		SHADER_PARAM( SELFILLUMMASK, SHADER_PARAM_TYPE_TEXTURE, "shadertest/BaseTexture", "If we bind a texture here, it overrides base alpha (if any) for self illum" )

	END_SHADER_PARAMS

	void SetupParmsGBuffer( defParms_gBuffer &p )
	{
		p.bModel = true;

		p.iAlbedo = BASETEXTURE;
		p.iBumpmap = BUMPMAP;
		p.iPhong = PHONG;
		p.iPhongmap = PHONGEXPONENTTEXTURE;
		p.iPhongmapFallback = PHONG_MAP;

		p.iAlphatestRef = ALPHATESTREFERENCE;
		p.iLitface = LITFACE;
		p.iPhongExp = PHONGEXPONENT;
		p.iPhongExpFallback = PHONG_EXP;
	}

	void SetupParmsShadow( defParms_shadow &p )
	{
		p.bModel = true;
		p.iAlbedo = BASETEXTURE;
		p.iAlphatestRef = ALPHATESTREFERENCE;
	}

	void SetupParmsComposite( defParms_composite &p )
	{
		p.bModel = true;
		p.iAlbedo = BASETEXTURE;
		p.iBumpmap = BUMPMAP;

		p.iEnvmap = ENVMAP;
		p.iEnvmapMask = ENVMAPMASK;
		p.iEnvmapTint = ENVMAPTINT;
		p.iEnvmapContrast = ENVMAPCONTRAST;
		p.iEnvmapSaturation = ENVMAPSATURATION;
		p.iEnvmapFresnel = ENVMAPFRESNEL;
		p.iBaseAlphaEnvmapMask = BASEALPHAENVMAPMASK;
		p.iNormalMapAlphaEnvmapMask = NORMALMAPALPHAENVMAPMASK;

		p.iRimlightEnable = RIMLIGHT;
		p.iRimlightExponent = RIMLIGHTEXPONENT;
		p.iRimlightAlbedoScale = RIMLIGHTALBEDOSCALE;
		p.iRimlightTint = RIMLIGHTTINT;
		p.iRimlightModLight = RIMLIGHT_MODULATE_BY_LIGHT;

		p.iAlphatestRef = ALPHATESTREFERENCE;

		p.iPhong = PHONG;
		p.iPhongBoost = PHONGBOOST;
		p.iPhongScale = PHONG_SCALE;
		p.iPhongFresnel = PHONG_FRESNEL;
		p.iPhongFresnelRanges = PHONGFRESNELRANGES;
		p.iFresnelRanges = FRESNELRANGES;
		p.iBaseMapAlphaPhongMask = BASEMAPALPHAPHONGMASK;
		p.iInvertPhongMask = INVERTPHONGMASK;
		p.iPhongAlbedoTint = PHONGALBEDOTINT;

		p.iSelfIllumTint = SELFILLUMTINT;
		p.iSelfIllumMaskInEnvmapAlpha = SELFILLUM_ENVMAPMASK_ALPHA;
		p.iSelfIllumFresnelModulate = SELFILLUMFRESNEL;
		p.iSelfIllumMask = SELFILLUMMASK;
	}

	bool DrawToGBuffer( IMaterialVar **params )
	{
#if DEFCFG_DEFERRED_SHADING == 1
		return true;
#else
		const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );
		const bool bTranslucent = IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT );

		return !bTranslucent && !bIsDecal;
#endif
	}

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );

		if ( g_pHardwareConfig->HasFastVertexTextures() )
			SET_FLAGS2( MATERIAL_VAR2_USES_VERTEXID );

		if ( PARM_SET( BASEALPHAENVMAPMASK ) )
			SET_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );

		if ( PARM_SET( NORMALMAPALPHAENVMAPMASK ) )
			SET_FLAGS( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK );

		const bool bDrawToGBuffer = DrawToGBuffer( params );

		if ( bDrawToGBuffer )
		{
			defParms_gBuffer parms_gbuffer;
			SetupParmsGBuffer( parms_gbuffer );
			InitParmsGBuffer( parms_gbuffer, this, params );

			defParms_shadow parms_shadow;
			SetupParmsShadow( parms_shadow );
			InitParmsShadowPass( parms_shadow, this, params );
		}

		defParms_composite parms_composite;
		SetupParmsComposite( parms_composite );
		InitParmsComposite( parms_composite, this, params );
	}

	SHADER_INIT
	{
		const bool bDrawToGBuffer = DrawToGBuffer( params );

		if ( bDrawToGBuffer )
		{
			defParms_gBuffer parms_gbuffer;
			SetupParmsGBuffer( parms_gbuffer );
			InitPassGBuffer( parms_gbuffer, this, params );

			defParms_shadow parms_shadow;
			SetupParmsShadow( parms_shadow );
			InitPassShadowPass( parms_shadow, this, params );
		}

		defParms_composite parms_composite;
		SetupParmsComposite( parms_composite );
		InitPassComposite( parms_composite, this, params );
	}

	SHADER_FALLBACK
	{
		if ( !GetDeferredExt()->IsDeferredLightingEnabled() )
			return "VertexlitGeneric";

		const bool bTranslucent = IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT );
		const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );

		if ( bTranslucent && !bIsDecal )
			return "VertexlitGeneric";

		return 0;
	}

	SHADER_DRAW
	{
		if ( pShaderAPI != NULL && *pContextDataPtr == NULL )
			*pContextDataPtr = new CDeferredPerMaterialContextData();

		CDeferredPerMaterialContextData *pDefContext = reinterpret_cast< CDeferredPerMaterialContextData* >(*pContextDataPtr);

		const int iDeferredRenderStage = pShaderAPI ?
			pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE )
			: DEFERRED_RENDER_STAGE_INVALID;

		const bool bDrawToGBuffer = DrawToGBuffer( params );

		Assert( pShaderAPI == NULL ||
			iDeferredRenderStage != DEFERRED_RENDER_STAGE_INVALID );

		if ( bDrawToGBuffer )
		{
			if ( pShaderShadow != NULL ||
				iDeferredRenderStage == DEFERRED_RENDER_STAGE_GBUFFER )
			{
				defParms_gBuffer parms_gbuffer;
				SetupParmsGBuffer( parms_gbuffer );
				DrawPassGBuffer( parms_gbuffer, this, params, pShaderShadow, pShaderAPI,
					vertexCompression, pDefContext );
			}
			else
				Draw(false);

			if ( pShaderShadow != NULL ||
				iDeferredRenderStage == DEFERRED_RENDER_STAGE_SHADOWPASS )
			{
				defParms_shadow parms_shadow;
				SetupParmsShadow( parms_shadow );
				DrawPassShadowPass( parms_shadow, this, params, pShaderShadow, pShaderAPI,
					vertexCompression, pDefContext );
			}
			else
				Draw(false);
		}

#if ( DEFCFG_DEFERRED_SHADING == 0 )
		if ( pShaderShadow != NULL ||
			iDeferredRenderStage == DEFERRED_RENDER_STAGE_COMPOSITION )
		{
			defParms_composite parms_composite;
			SetupParmsComposite( parms_composite );
			DrawPassComposite( parms_composite, this, params, pShaderShadow, pShaderAPI,
				vertexCompression, pDefContext );
		}
		else
			Draw(false);
#endif

		if ( pShaderAPI != NULL && pDefContext->m_bMaterialVarsChanged )
			pDefContext->m_bMaterialVarsChanged = false;
	}

END_SHADER