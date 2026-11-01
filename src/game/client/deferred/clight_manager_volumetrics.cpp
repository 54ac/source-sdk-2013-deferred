#include "cbase.h"
#include "viewrender.h"
#include "view_shared.h"
#include "materialsystem/imaterialvar.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_client_common.h"
#include "deferred/deferred_rt.h"
#include "deferred/clight_manager.h"

#include "tier0/memdbgon.h"

void CLightingManager::RenderVolumetrics( const CViewSetup& view )
{
	if ( !m_bDrawVolumetrics )
		return;

	const float flIntensity = r_deferred_volumetric_intensity.GetFloat();
	if ( flIntensity <= 0.0f )
		return;

	ITexture* pVolumBuffer0 = GetDefRT_VolumetricsBuffer( 0 );
	ITexture* pVolumBuffer1 = GetDefRT_VolumetricsBuffer( 1 );
	const int rtW = pVolumBuffer0 ? pVolumBuffer0->GetActualWidth() : ( view.width / 4 );
	const int rtH = pVolumBuffer0 ? pVolumBuffer0->GetActualHeight() : ( view.height / 4 );

	CMatRenderContextPtr pRenderContext( materials );

	const float flBlur = r_deferred_volumetric_blur.GetFloat();
	if ( flBlur > 0.0f )
	{
		IMaterial* pMatBlurX = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_BLUR_G6_X );
		IMaterial* pMatBlurY = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_BLUR_G6_Y );

		if ( pMatBlurX != NULL && pMatBlurY != NULL )
		{
			bool bFoundX = false, bFoundY = false;
			IMaterialVar* pBlurSizeX = pMatBlurX->FindVar( "$BLURSIZE", &bFoundX, true );
			IMaterialVar* pBlurSizeY = pMatBlurY->FindVar( "$BLURSIZE", &bFoundY, true );

			const int iPasses = Clamp( RoundFloatToInt( flBlur * 3.0f ), 2, 4 );
			static const float flPassScale[4] = { 1.5f, 3.5f, 7.0f, 12.0f };

			for ( int p = 0; p < iPasses; p++ )
			{
				const float flStep = flPassScale[p] * flBlur;
				if ( pBlurSizeX != NULL )
					pBlurSizeX->SetFloatValue( flStep );
				if ( pBlurSizeY != NULL )
					pBlurSizeY->SetFloatValue( flStep );

				pRenderContext->PushRenderTargetAndViewport( pVolumBuffer1, 0, 0, view.width / 4, view.height / 4 );
				pRenderContext->DrawScreenSpaceRectangle( pMatBlurX,
				                                          0, 0, view.width / 4, view.height / 4,
				                                          0, 0, view.width / 4 - 1, view.height / 4 - 1,
				                                          rtW, rtH );
				pRenderContext->PopRenderTargetAndViewport();

				pRenderContext->PushRenderTargetAndViewport( pVolumBuffer0, 0, 0, view.width / 4, view.height / 4 );
				pRenderContext->DrawScreenSpaceRectangle( pMatBlurY,
				                                          0, 0, view.width / 4, view.height / 4,
				                                          0, 0, view.width / 4 - 1, view.height / 4 - 1,
				                                          rtW, rtH );
				pRenderContext->PopRenderTargetAndViewport();
			}
		}
	}

	IMaterial* pMatBlend = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_BLEND );
	if ( pMatBlend != NULL )
	{
		bool bFound = false;
		IMaterialVar* pIntensityVar = pMatBlend->FindVar( "$INTENSITY", &bFound, true );
		if ( pIntensityVar != NULL )
		{
			pIntensityVar->SetFloatValue( flIntensity );
		}
	}

	pRenderContext->DrawScreenSpaceRectangle( pMatBlend,
	                                          0, 0, view.width, view.height,
	                                          0, 0, view.width / 4 - 1, view.height / 4 - 1,
	                                          rtW, rtH );
}
