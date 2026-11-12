#include "deferred_includes.h"

#include "quad_deferred_vs30.inc"
#include "quad_deferred_ps30.inc"

#include "tier0/memdbgon.h"

static inline void SetupQuadVertexFormat( IShaderShadow *pShaderShadow, int nNormalMode )
{
	if ( nNormalMode == 1 )
	{
		int tCoordDimensions[] = { 2, 2 };
		pShaderShadow->VertexShaderVertexFormat(
			VERTEX_POSITION | VERTEX_COLOR | VERTEX_TANGENT_S | VERTEX_TANGENT_T,
			2, tCoordDimensions, 0 );
	}
	else
	{
		int pTexCoordDim[1] = { 2 };
		pShaderShadow->VertexShaderVertexFormat(
			VERTEX_POSITION | VERTEX_COLOR,
			1, pTexCoordDim, 0 );
	}
}

static inline void SetupQuadCameraConstants( IShaderDynamicAPI *pShaderAPI )
{
	float vPos[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	pShaderAPI->GetWorldSpaceCameraPosition( vPos );
	float zScale[4] = { GetDeferredExt()->GetZScale(), 0.0f, 0.0f, 0.0f };
	pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, vPos );
	pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, GetDeferredExt()->GetForwardBase() );
	pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, zScale );
}

BEGIN_VS_SHADER( DEFERRED_QUAD, "Deferred Quad Shader" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0.4", "Alpha test reference value" )
		SHADER_PARAM( NORMALMODE, SHADER_PARAM_TYPE_INTEGER, "0", "0 = Upward foliage, 1 = Cable cylindrical" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT_PARAMS()
	{
		if ( !params[ ALPHATESTREFERENCE ]->IsDefined() )
		{
			params[ ALPHATESTREFERENCE ]->SetFloatValue( 0.4f );
		}
		if ( !params[ NORMALMODE ]->IsDefined() )
		{
			params[ NORMALMODE ]->SetIntValue( 0 );
		}
	}

	SHADER_INIT
	{
		LoadTexture( BASETEXTURE );
	}

	SHADER_DRAW
	{
		const int iDeferredRenderStage = pShaderAPI != NULL
			? pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE )
			: DEFERRED_RENDER_STAGE_INVALID;

		const int nNormalMode = clamp( params[ NORMALMODE ]->GetIntValue(), 0, 1 );
		const bool bAlphaTest = ( nNormalMode == 0 ) || IS_FLAG_SET( MATERIAL_VAR_ALPHATEST );
		const bool bVertexColor = IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR );
		const bool bNoCull = IS_FLAG_SET( MATERIAL_VAR_NOCULL );
		const float flAlphaRef = params[ ALPHATESTREFERENCE ]->GetFloatValue();

		// PASS 0: G-BUFFER PASS
		if ( pShaderShadow != NULL || iDeferredRenderStage == DEFERRED_RENDER_STAGE_GBUFFER )
		{
			SHADOW_STATE
			{
				pShaderShadow->SetDefaultState();

				pShaderShadow->EnableSRGBWrite( false );
				pShaderShadow->EnableAlphaWrites( true );
				pShaderShadow->EnableDepthWrites( true );

				if ( bNoCull )
				{
					pShaderShadow->EnableCulling( false );
				}

				pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );

				SetupQuadVertexFormat( pShaderShadow, nNormalMode );

				DECLARE_STATIC_VERTEX_SHADER( quad_deferred_vs30 );
				SET_STATIC_VERTEX_SHADER_COMBO( STAGE, 0 );
				SET_STATIC_VERTEX_SHADER_COMBO( NORMAL_MODE, nNormalMode );
				SET_STATIC_VERTEX_SHADER( quad_deferred_vs30 );

				DECLARE_STATIC_PIXEL_SHADER( quad_deferred_ps30 );
				SET_STATIC_PIXEL_SHADER_COMBO( STAGE, 0 ); // GBUFFER
				SET_STATIC_PIXEL_SHADER_COMBO( NORMAL_MODE, nNormalMode );
				SET_STATIC_PIXEL_SHADER_COMBO( ALPHATEST, bAlphaTest ? 1 : 0 );
				SET_STATIC_PIXEL_SHADER_COMBO( VERTEXCOLOR, bVertexColor ? 1 : 0 );
				SET_STATIC_PIXEL_SHADER( quad_deferred_ps30 );
			}
			DYNAMIC_STATE
			{
				BindTexture( SHADER_SAMPLER0, BASETEXTURE );

				if ( bAlphaTest )
				{
					float flConstAlphaRef[4] = { flAlphaRef, 0.0f, 0.0f, 0.0f };
					pShaderAPI->SetPixelShaderConstant( 0, flConstAlphaRef, 1 );
				}

				SetupQuadCameraConstants( pShaderAPI );

				DECLARE_DYNAMIC_VERTEX_SHADER( quad_deferred_vs30 );
				SET_DYNAMIC_VERTEX_SHADER( quad_deferred_vs30 );

				DECLARE_DYNAMIC_PIXEL_SHADER( quad_deferred_ps30 );
				SET_DYNAMIC_PIXEL_SHADER( quad_deferred_ps30 );
			}
			Draw();
		}
		else
		{
			Draw( false );
		}

		// PASS 1: COMPOSITION PASS
		if ( pShaderShadow != NULL || iDeferredRenderStage == DEFERRED_RENDER_STAGE_COMPOSITION )
		{
			SHADOW_STATE
			{
				pShaderShadow->SetDefaultState();

				pShaderShadow->EnableSRGBWrite( true );
				pShaderShadow->EnableAlphaWrites( false );
				pShaderShadow->EnableDepthWrites( true );

				if ( bNoCull )
				{
					pShaderShadow->EnableCulling( false );
				}

				pShaderShadow->EnableTexture( SHADER_SAMPLER0, true ); // Albedo
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );

				pShaderShadow->EnableTexture( SHADER_SAMPLER2, true ); // sLightAccum
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, false );

				SetupQuadVertexFormat( pShaderShadow, nNormalMode );

				DECLARE_STATIC_VERTEX_SHADER( quad_deferred_vs30 );
				SET_STATIC_VERTEX_SHADER_COMBO( STAGE, 1 );
				SET_STATIC_VERTEX_SHADER_COMBO( NORMAL_MODE, nNormalMode );
				SET_STATIC_VERTEX_SHADER( quad_deferred_vs30 );

				DECLARE_STATIC_PIXEL_SHADER( quad_deferred_ps30 );
				SET_STATIC_PIXEL_SHADER_COMBO( STAGE, 1 ); // COMPOSITION
				SET_STATIC_PIXEL_SHADER_COMBO( NORMAL_MODE, nNormalMode );
				SET_STATIC_PIXEL_SHADER_COMBO( ALPHATEST, bAlphaTest ? 1 : 0 );
				SET_STATIC_PIXEL_SHADER_COMBO( VERTEXCOLOR, bVertexColor ? 1 : 0 );
				SET_STATIC_PIXEL_SHADER( quad_deferred_ps30 );
			}
			DYNAMIC_STATE
			{
				BindTexture( SHADER_SAMPLER0, BASETEXTURE );

				ITexture *pLightAccum = GetDeferredExt()->GetTexture_LightAccum();
				if ( pLightAccum )
				{
					BindTexture( SHADER_SAMPLER2, pLightAccum );
				}
				else
				{
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_BLACK );
				}

				if ( bAlphaTest )
				{
					float flConstAlphaRef[4] = { flAlphaRef, 0.0f, 0.0f, 0.0f };
					pShaderAPI->SetPixelShaderConstant( 0, flConstAlphaRef, 1 );
				}

				int texW = pLightAccum ? pLightAccum->GetActualWidth() : 0;
				int texH = pLightAccum ? pLightAccum->GetActualHeight() : 0;
				if ( texW <= 0 || texH <= 0 )
				{
					ShaderViewport_t vp;
					pShaderAPI->GetViewports( &vp, 1 );
					texW = vp.m_nWidth;
					texH = vp.m_nHeight;
				}
				float flTexel[4] = { texW > 0 ? ( 1.0f / (float)texW ) : 0.0f,
									 texH > 0 ? ( 1.0f / (float)texH ) : 0.0f,
									 0.0f, 0.0f };
				pShaderAPI->SetPixelShaderConstant( 1, flTexel );

				DECLARE_DYNAMIC_VERTEX_SHADER( quad_deferred_vs30 );
				SET_DYNAMIC_VERTEX_SHADER( quad_deferred_vs30 );

				DECLARE_DYNAMIC_PIXEL_SHADER( quad_deferred_ps30 );
				SET_DYNAMIC_PIXEL_SHADER( quad_deferred_ps30 );
			}
			Draw();
		}
		else
		{
			Draw( false );
		}
	}

END_SHADER
