#ifndef LIGHTING_HELPER_H
#define LIGHTING_HELPER_H

#include "deferred_includes.h"

FORCEINLINE void CommitBaseDeferredConstants_Frustum( IShaderDynamicAPI* pShaderAPI,
	const int iFirstFrustumRegister, const bool bVertexShader = true )
{
	if ( bVertexShader )
		pShaderAPI->SetVertexShaderConstant( iFirstFrustumRegister, GetDeferredExt()->GetFrustumDeltaBase(), 3 );
	else
		pShaderAPI->SetPixelShaderConstant( iFirstFrustumRegister, GetDeferredExt()->GetFrustumDeltaBase(), 3 );
}

FORCEINLINE void CommitBaseDeferredConstants_Origin( IShaderDynamicAPI* pShaderAPI,
	const int iPixelShaderOriginRegister )
{
	pShaderAPI->SetPixelShaderConstant( iPixelShaderOriginRegister, GetDeferredExt()->GetOriginBase() );
}


FORCEINLINE void CommitShadowcastingConstants_Ortho( IShaderDynamicAPI *pShaderAPI, const int index,
	int iForwardRegister, int iSlopeRegister, int iOriginRegister )
{
	const Vector4D& fwd = -GetDeferredExt()->GetLightData_Global().vecLight;
	const shadowData_ortho_t &data = GetDeferredExt()->GetShadowData_Ortho( index );

	pShaderAPI->SetVertexShaderConstant( iForwardRegister, fwd.Base() );
	pShaderAPI->SetVertexShaderConstant( iSlopeRegister, data.vecSlopeSettings.Base() );
	pShaderAPI->SetVertexShaderConstant( iOriginRegister, data.vecOrigin.Base() );
}

FORCEINLINE void CommitShadowcastingConstants_Proj( IShaderDynamicAPI *pShaderAPI, const int index,
	int iForwardRegister, int iSlopeRegister, int iOriginRegister )
{
	const shadowData_proj_t &data = GetDeferredExt()->GetShadowData_Proj( index );

	pShaderAPI->SetVertexShaderConstant( iForwardRegister, data.vecForward.Base() );
	pShaderAPI->SetVertexShaderConstant( iSlopeRegister, data.vecSlopeSettings.Base() );
	pShaderAPI->SetVertexShaderConstant( iOriginRegister, data.vecOrigin.Base() );
}

FORCEINLINE void CommitGlobalLightForward( IShaderDynamicAPI *pShaderAPI,
	int iFirstRegister )
{
	const Vector4D& vecLight = GetDeferredExt()->GetLightData_Global().vecLight;
	pShaderAPI->SetPixelShaderConstant( iFirstRegister++, vecLight.Base() );
}

FORCEINLINE void MakeShadowProjectionConstants( float *pFl0, float *pFl1, int resx, int resy )
{
	static ConVarRef r_deferred_shadow_filter_scale( "r_deferred_shadow_filter_scale" );
	const float flScale = r_deferred_shadow_filter_scale.IsValid() ? r_deferred_shadow_filter_scale.GetFloat() : 1.6f;

	pFl0[0] = flScale / resx;
	pFl0[1] = flScale / resy;
	pFl0[2] = ( flScale * 2.0f ) / resx;
	pFl0[3] = ( flScale * 2.0f ) / resy;

	pFl1[0] = resx;
	pFl1[1] = resy;
}

FORCEINLINE void CommitShadowProjectionConstants_Ortho_Single( IShaderDynamicAPI *pShaderAPI,
	const int index, int iFirstRegister )
{
	const shadowData_ortho_t &data = GetDeferredExt()->GetShadowData_Ortho( index );
	const Vector4D& fwd = -GetDeferredExt()->GetLightData_Global().vecLight;

	pShaderAPI->SetPixelShaderConstant( iFirstRegister, data.matWorldToTexture.Base(), 3 );
	iFirstRegister += 3;
	pShaderAPI->SetPixelShaderConstant( iFirstRegister++, data.vecUVTransform.Base() );
	pShaderAPI->SetPixelShaderConstant( iFirstRegister++, data.vecSlopeSettings.Base() );
	pShaderAPI->SetPixelShaderConstant( iFirstRegister++, data.vecOrigin.Base() );
	pShaderAPI->SetPixelShaderConstant( iFirstRegister, fwd.Base() );
}

FORCEINLINE void CommitShadowProjectionConstants_Ortho_Composite( IShaderDynamicAPI *pShaderAPI,
	const int numCascades, int iFirstRegister )
{
	int stepsLeft = numCascades;
	for ( int i = 0; i < numCascades; i++ )
	{
		const shadowData_ortho_t &data = GetDeferredExt()->GetShadowData_Ortho( i );
		int iCurRegister = iFirstRegister + i * 3;

		pShaderAPI->SetPixelShaderConstant( iCurRegister, data.matWorldToTexture.Base(), 3 );
		iCurRegister += i;
		iCurRegister += stepsLeft * 3;
		pShaderAPI->SetPixelShaderConstant( iCurRegister, data.vecUVTransform.Base() );
		iCurRegister += numCascades;
		pShaderAPI->SetPixelShaderConstant( iCurRegister, data.vecSlopeSettings.Base() );
		iCurRegister += numCascades;

		float fl_0[4] = { 0, 0, 0, 0 };
		float fl_1[4] = { 0, 0, 0, 0 };

		MakeShadowProjectionConstants( fl_0, fl_1, data.iRes_x, data.iRes_y );

		pShaderAPI->SetPixelShaderConstant( iCurRegister, fl_0 );
		iCurRegister += numCascades;
		pShaderAPI->SetPixelShaderConstant( iCurRegister, fl_1 );

		stepsLeft--;
	}
}

FORCEINLINE void CommitCSMDistances( IShaderDynamicAPI *pShaderAPI, int iConstantRegister )
{
	static ConVarRef r_deferred_csm_max_dist( "r_deferred_csm_max_dist" );
	static ConVarRef r_deferred_csm_split_dist( "r_deferred_csm_split_dist" );
	float flSplitDist = r_deferred_csm_split_dist.IsValid() ? r_deferred_csm_split_dist.GetFloat() : 1024.0f;
	float flMaxDist = r_deferred_csm_max_dist.IsValid() ? r_deferred_csm_max_dist.GetFloat() : 4096.0f;
	if ( flSplitDist <= 64.0f ) flSplitDist = 1024.0f;
	if ( flMaxDist <= flSplitDist ) flMaxDist = flSplitDist * 4.0f;
	float csmDist[4] = { flSplitDist, flMaxDist, 1.0f / ( flSplitDist * 0.15f ), 1.0f / ( flMaxDist * 0.15f ) };
	pShaderAPI->SetPixelShaderConstant( iConstantRegister, csmDist );
}

FORCEINLINE void CommitPointLightingConstants( IShaderDynamicAPI *pShaderAPI, int iConstantRegister )
{
	static ConVarRef r_deferred_point_light_half_lambert( "r_deferred_point_light_half_lambert" );
	float flHalfLambert = r_deferred_point_light_half_lambert.IsValid() ? r_deferred_point_light_half_lambert.GetFloat() : 1.0f;
	float tweaks[4] = { clamp( flHalfLambert, 0.0f, 1.0f ), 0.0f, 0.0f, 0.0f };
	pShaderAPI->SetPixelShaderConstant( iConstantRegister, tweaks );
}

FORCEINLINE void CommitShadowProjectionConstants_DPSM( IShaderDynamicAPI *pShaderAPI,
	int iFirstRegister )
{
	const shadowData_general_t& data = GetDeferredExt()->GetShadowData_General();

	float fl_0[4] = { 0, 0, 0, 0 };
	float fl_1[4] = { 0, 0, 0, 0 };

	MakeShadowProjectionConstants( fl_0, fl_1, data.iDPSM_Res_x, data.iDPSM_Res_y );

	pShaderAPI->SetPixelShaderConstant( iFirstRegister++, fl_0 );
	pShaderAPI->SetPixelShaderConstant( iFirstRegister, fl_1 );
}

FORCEINLINE void CommitShadowProjectionConstants_Proj( IShaderDynamicAPI *pShaderAPI,
	int iFirstRegister )
{
	const shadowData_general_t& data = GetDeferredExt()->GetShadowData_General();

	float fl_0[4] = { 0, 0, 0, 0 };
	float fl_1[4] = { 0, 0, 0, 0 };

	MakeShadowProjectionConstants( fl_0, fl_1, data.iPROJ_Res, data.iPROJ_Res );

	pShaderAPI->SetPixelShaderConstant( iFirstRegister++, fl_0 );
	pShaderAPI->SetPixelShaderConstant( iFirstRegister, fl_1 );
}

FORCEINLINE void CommitViewVertexShader( IShaderDynamicAPI *pShaderAPI,
	int iRegView )
{
	VMatrix v;
	v.Identity();
	pShaderAPI->GetMatrix( MATERIAL_VIEW, v.Base() );
	pShaderAPI->SetVertexShaderConstant( iRegView, v.Transpose().Base(), 3 );
}

FORCEINLINE Vector4D MakeHalfAmbient( const Vector4D& ambient_low, const Vector4D& ambient_high )
{
	Vector4D lowDelta = ambient_low - ambient_high;
	lowDelta = ambient_high + lowDelta * 0.5f;
	for (int i = 0; i < 3; i++)
		lowDelta[i] = Max(0.f, lowDelta[i]);
	return lowDelta;
}

FORCEINLINE void CommitFullScreenTexel( IShaderDynamicAPI *pShaderAPI,
	int iConstantRegister, float flScale = 1.0f )
{
	int w, t;
	pShaderAPI->GetBackBufferDimensions( w, t );
	float fl4[4] = { flScale / w, flScale / t, 0, 0 };
	pShaderAPI->SetPixelShaderConstant( iConstantRegister, fl4 );
}

FORCEINLINE void CommitHalfScreenTexel( IShaderDynamicAPI *pShaderAPI,
	int iConstantRegister, float flScale = 1.0f )
{
	CommitFullScreenTexel( pShaderAPI, iConstantRegister, flScale * 0.5f );
}

#endif