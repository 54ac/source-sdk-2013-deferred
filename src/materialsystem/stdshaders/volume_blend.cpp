
#include "deferred_includes.h"

#include "screenspace_vs30.inc"
#include "volume_blend_ps30.inc"

BEGIN_VS_SHADER( VOLUME_BLEND, "Deferred Volumetric Fog Blend Pass" )
	BEGIN_SHADER_PARAMS

		SHADER_PARAM( INTENSITY, SHADER_PARAM_TYPE_FLOAT, "1.0", "Intensity multiplier" )

	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		if ( !params[ INTENSITY ]->IsDefined() )
			params[ INTENSITY ]->SetFloatValue( 1.0f );
	}

	SHADER_INIT
	{
		Assert( params[ BASETEXTURE ]->IsDefined() );

		if ( params[ BASETEXTURE ]->IsDefined() )
			LoadTexture( BASETEXTURE );
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->SetDefaultState();

			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( screenspace_vs30 );
			SET_STATIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( volume_blend_ps30 );
			SET_STATIC_PIXEL_SHADER( volume_blend_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( volume_blend_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( volume_blend_ps30 );

			BindTexture( SHADER_SAMPLER0, BASETEXTURE );

			float flIntensity = params[ INTENSITY ]->IsDefined() ? params[ INTENSITY ]->GetFloatValue() : 1.0f;
			float flParams[4] = { flIntensity, 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, flParams );
		}

		Draw();
	}
END_SHADER