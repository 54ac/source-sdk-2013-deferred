
#include "deferred_includes.h"

#include "tier0/memdbgon.h"

// DEFINE_FALLBACK_SHADER( VertexLitGeneric, DEFERRED_MODEL )
// DEFINE_FALLBACK_SHADER( UnlitGeneric, DEFERRED_MODEL )

BEGIN_VS_SHADER( DEFERRED_MODEL, "Deferred Model Shader" )
	BEGIN_SHADER_PARAMS

		SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "", "Normal/bump map texture" )

		SHADER_PARAM( LITFACE, SHADER_PARAM_TYPE_BOOL, "0", "Force per-face lighting evaluation" )

		SHADER_PARAM( PHONG, SHADER_PARAM_TYPE_BOOL, "0", "enables phong specular" )
		SHADER_PARAM( PHONGBOOST, SHADER_PARAM_TYPE_FLOAT, "1.0", "phong specular boost" )
		SHADER_PARAM( PHONGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "20.0", "phong exponent" )
		SHADER_PARAM( PHONGEXPONENTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "phong exponent texture" )
		SHADER_PARAM( PHONGFRESNELRANGES, SHADER_PARAM_TYPE_VEC3, "[0.05 0.5 1]", "phong fresnel ranges" )
		SHADER_PARAM( BASEMAPALPHAPHONGMASK, SHADER_PARAM_TYPE_BOOL, "0", "use basemap alpha for phong mask" )
		SHADER_PARAM( INVERTPHONGMASK, SHADER_PARAM_TYPE_BOOL, "0", "invert phong mask" )
		SHADER_PARAM( PHONGALBEDOTINT, SHADER_PARAM_TYPE_BOOL, "0", "tint phong with albedo" )

		SHADER_PARAM( PHONG_SCALE, SHADER_PARAM_TYPE_FLOAT, "", "Phong specular scaling factor" )
		SHADER_PARAM( PHONG_EXP, SHADER_PARAM_TYPE_FLOAT, "", "Phong specular exponent" )
		SHADER_PARAM( PHONG_MAP, SHADER_PARAM_TYPE_TEXTURE, "", "Phong specular exponent/mask texture" )
		SHADER_PARAM( PHONG_FRESNEL, SHADER_PARAM_TYPE_BOOL, "", "Enable phong fresnel term" )

		SHADER_PARAM( FRESNELRANGES, SHADER_PARAM_TYPE_VEC3, "", "Fresnel reflection ranges [min mid max]" )
		SHADER_PARAM( ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0.5", "Alpha test reference value for cutout models" )

		SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "shadertest/shadertest_env", "envmap" )
		SHADER_PARAM( ENVMAPTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "envmap tint" )
		SHADER_PARAM( ENVMAPCONTRAST, SHADER_PARAM_TYPE_FLOAT, "0.0", "contrast 0 == normal 1 == color*color" )
		SHADER_PARAM( ENVMAPSATURATION, SHADER_PARAM_TYPE_FLOAT, "1.0", "saturation 0 == greyscale 1 == normal" )
		SHADER_PARAM( ENVMAPFRESNEL, SHADER_PARAM_TYPE_BOOL, "", "Fresnel-weighted environment map reflection" )
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

		SHADER_PARAM( TREESWAY, SHADER_PARAM_TYPE_INTEGER, "0", "Tree sway wind animation mode (0 = off, 1 = leaves, 2 = full trunk)" )
		SHADER_PARAM( TREESWAYHEIGHT, SHADER_PARAM_TYPE_FLOAT, "1000", "Tree sway reference height" )
		SHADER_PARAM( TREESWAYSTARTHEIGHT, SHADER_PARAM_TYPE_FLOAT, "0.2", "Fractional height where trunk sway begins" )
		SHADER_PARAM( TREESWAYRADIUS, SHADER_PARAM_TYPE_FLOAT, "300", "Radius of tree trunk sway influence" )
		SHADER_PARAM( TREESWAYSTARTRADIUS, SHADER_PARAM_TYPE_FLOAT, "0.1", "Fractional radius where branch sway begins" )
		SHADER_PARAM( TREESWAYSPEED, SHADER_PARAM_TYPE_FLOAT, "1", "Speed multiplier for wind animation" )
		SHADER_PARAM( TREESWAYSPEEDHIGHWINDMULTIPLIER, SHADER_PARAM_TYPE_FLOAT, "2", "Speed multiplier during high wind gusts" )
		SHADER_PARAM( TREESWAYSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "10", "Displacement amplitude for wind sway" )
		SHADER_PARAM( TREESWAYSCRUMBLESPEED, SHADER_PARAM_TYPE_FLOAT, "0.1", "Leaf flutter/scrumble speed" )
		SHADER_PARAM( TREESWAYSCRUMBLESTRENGTH, SHADER_PARAM_TYPE_FLOAT, "0.1", "Leaf flutter/scrumble displacement strength" )
		SHADER_PARAM( TREESWAYSCRUMBLEFREQUENCY, SHADER_PARAM_TYPE_FLOAT, "0.1", "Spatial frequency for leaf scrumble" )
		SHADER_PARAM( TREESWAYFALLOFFEXP, SHADER_PARAM_TYPE_FLOAT, "1.5", "Height falloff exponent for trunk sway" )
		SHADER_PARAM( TREESWAYSCRUMBLEFALLOFFEXP, SHADER_PARAM_TYPE_FLOAT, "1.0", "Radial falloff exponent for leaf scrumble" )
		SHADER_PARAM( TREESWAYSPEEDLERPSTART, SHADER_PARAM_TYPE_FLOAT, "3", "Wind speed lerp start threshold" )
		SHADER_PARAM( TREESWAYSPEEDLERPEND, SHADER_PARAM_TYPE_FLOAT, "6", "Wind speed lerp end threshold" )
		SHADER_PARAM( TREESWAYSTATIC, SHADER_PARAM_TYPE_BOOL, "0", "Static branch anchoring mode" )

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

		p.iTreeSway = TREESWAY;
		p.iTreeSwayHeight = TREESWAYHEIGHT;
		p.iTreeSwayStartHeight = TREESWAYSTARTHEIGHT;
		p.iTreeSwayRadius = TREESWAYRADIUS;
		p.iTreeSwayStartRadius = TREESWAYSTARTRADIUS;
		p.iTreeSwaySpeed = TREESWAYSPEED;
		p.iTreeSwaySpeedHighWindMultiplier = TREESWAYSPEEDHIGHWINDMULTIPLIER;
		p.iTreeSwayStrength = TREESWAYSTRENGTH;
		p.iTreeSwayScrumbleSpeed = TREESWAYSCRUMBLESPEED;
		p.iTreeSwayScrumbleStrength = TREESWAYSCRUMBLESTRENGTH;
		p.iTreeSwayScrumbleFrequency = TREESWAYSCRUMBLEFREQUENCY;
		p.iTreeSwayFalloffExp = TREESWAYFALLOFFEXP;
		p.iTreeSwayScrumbleFalloffExp = TREESWAYSCRUMBLEFALLOFFEXP;
		p.iTreeSwaySpeedLerpStart = TREESWAYSPEEDLERPSTART;
		p.iTreeSwaySpeedLerpEnd = TREESWAYSPEEDLERPEND;
		p.iTreeSwayStatic = TREESWAYSTATIC;
	}

	void SetupParmsShadow( defParms_shadow &p )
	{
		p.bModel = true;
		p.iAlbedo = BASETEXTURE;
		p.iAlphatestRef = ALPHATESTREFERENCE;

		p.iTreeSway = TREESWAY;
		p.iTreeSwayHeight = TREESWAYHEIGHT;
		p.iTreeSwayStartHeight = TREESWAYSTARTHEIGHT;
		p.iTreeSwayRadius = TREESWAYRADIUS;
		p.iTreeSwayStartRadius = TREESWAYSTARTRADIUS;
		p.iTreeSwaySpeed = TREESWAYSPEED;
		p.iTreeSwaySpeedHighWindMultiplier = TREESWAYSPEEDHIGHWINDMULTIPLIER;
		p.iTreeSwayStrength = TREESWAYSTRENGTH;
		p.iTreeSwayScrumbleSpeed = TREESWAYSCRUMBLESPEED;
		p.iTreeSwayScrumbleStrength = TREESWAYSCRUMBLESTRENGTH;
		p.iTreeSwayScrumbleFrequency = TREESWAYSCRUMBLEFREQUENCY;
		p.iTreeSwayFalloffExp = TREESWAYFALLOFFEXP;
		p.iTreeSwayScrumbleFalloffExp = TREESWAYSCRUMBLEFALLOFFEXP;
		p.iTreeSwaySpeedLerpStart = TREESWAYSPEEDLERPSTART;
		p.iTreeSwaySpeedLerpEnd = TREESWAYSPEEDLERPEND;
		p.iTreeSwayStatic = TREESWAYSTATIC;
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

		p.iTreeSway = TREESWAY;
		p.iTreeSwayHeight = TREESWAYHEIGHT;
		p.iTreeSwayStartHeight = TREESWAYSTARTHEIGHT;
		p.iTreeSwayRadius = TREESWAYRADIUS;
		p.iTreeSwayStartRadius = TREESWAYSTARTRADIUS;
		p.iTreeSwaySpeed = TREESWAYSPEED;
		p.iTreeSwaySpeedHighWindMultiplier = TREESWAYSPEEDHIGHWINDMULTIPLIER;
		p.iTreeSwayStrength = TREESWAYSTRENGTH;
		p.iTreeSwayScrumbleSpeed = TREESWAYSCRUMBLESPEED;
		p.iTreeSwayScrumbleStrength = TREESWAYSCRUMBLESTRENGTH;
		p.iTreeSwayScrumbleFrequency = TREESWAYSCRUMBLEFREQUENCY;
		p.iTreeSwayFalloffExp = TREESWAYFALLOFFEXP;
		p.iTreeSwayScrumbleFalloffExp = TREESWAYSCRUMBLEFALLOFFEXP;
		p.iTreeSwaySpeedLerpStart = TREESWAYSPEEDLERPSTART;
		p.iTreeSwaySpeedLerpEnd = TREESWAYSPEEDLERPEND;
		p.iTreeSwayStatic = TREESWAYSTATIC;
	}

	bool DrawToGBuffer( IMaterialVar **params )
	{
		const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );
		const bool bTranslucent = IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT );

		return !bTranslucent && !bIsDecal;
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

		if ( pShaderAPI != NULL && pDefContext->m_bMaterialVarsChanged )
			pDefContext->m_bMaterialVarsChanged = false;
	}

END_SHADER
