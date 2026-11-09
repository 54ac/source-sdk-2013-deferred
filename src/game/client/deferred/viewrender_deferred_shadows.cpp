#include "cbase.h"
#include "deferred/viewrender_deferred_internal.h"
#include "tier0/memdbgon.h"

void CBaseShadowView::Setup( const CViewSetup &view, ITexture *pDepthTexture, ITexture *pDummyTexture, const CViewSetup *pReflView )
{
	m_pDepthTexture = pDepthTexture;
	m_pDummyTexture = pDummyTexture;
	m_CameraView = view;
	m_bHasReflectionView = ( pReflView != NULL );
	if ( pReflView )
	{
		m_ReflectionView = *pReflView;
	}

	BaseClass::Setup( view );

	m_DrawFlags = DF_DRAW_ENTITITES | DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
	m_ClearFlags = 0;

	CalcShadowView();

	m_pCustomVisibility = &shadowVis;
	shadowVis = ViewCustomVisibility_t();
	shadowVis.AddVisOrigin( origin );
	shadowVis.AddVisOrigin( view.origin );
	if ( pReflView )
	{
		shadowVis.AddVisOrigin( pReflView->origin );
	}
}

void CBaseShadowView::SetupRadiosityTargets( ITexture *pAlbedoTexture, ITexture *pNormalTexture )
{
	m_pRadAlbedoTexture = pAlbedoTexture;
	m_pRadNormalTexture = pNormalTexture;
}

void CBaseShadowView::Draw()
{
	C_BaseEntity *pIgnore = ( m_pLight && m_pLight->m_hIgnoreEntity.Get() ) ? m_pLight->m_hIgnoreEntity.Get() : NULL;
	CUtlVectorFixed<C_BaseEntity*, 16> vecRestoredEntities;
	if ( pIgnore )
	{
		if ( !pIgnore->IsEffectActive( EF_NOSHADOW ) )
		{
			pIgnore->AddEffects( EF_NOSHADOW );
			vecRestoredEntities.AddToTail( pIgnore );
		}
		for ( C_BaseEntity *pChild = pIgnore->FirstMoveChild(); pChild != NULL; pChild = pChild->NextMovePeer() )
		{
			if ( !pChild->IsEffectActive( EF_NOSHADOW ) )
			{
				pChild->AddEffects( EF_NOSHADOW );
				if ( vecRestoredEntities.Count() < vecRestoredEntities.NumAllocated() )
				{
					vecRestoredEntities.AddToTail( pChild );
				}
			}
		}
	}

	const view_id_t oldViewID = g_CurrentViewID;
	SetupCurrentView( origin, angles, VIEW_DEFERRED_SHADOW );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_SHADOWPASS );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_MODE,
		GetShadowMode() );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_RADIOSITY,
		m_bOutputRadiosity ? 1 : 0 );
	pRenderContext.SafeRelease();

	DrawSetup( 0, m_DrawFlags, 0, -1, true );

	DrawExecute( 0, CurrentViewID(), 0, true );

	pRenderContext.GetFrom( materials );
		pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_RADIOSITY,
		0 );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_INVALID );

	g_CurrentViewID = oldViewID;

	for ( int i = 0; i < vecRestoredEntities.Count(); ++i )
	{
		if ( vecRestoredEntities[i] )
		{
			vecRestoredEntities[i]->RemoveEffects( EF_NOSHADOW );
		}
	}
}

bool CBaseShadowView::AdjustView( float waterHeight )
{
	return false;
}

void CBaseShadowView::DrawSetup( float waterHeight, int nSetupFlags, float waterZAdjust, int iForceViewLeaf, bool bShadowDepth )
{
	CommitData();

	const view_id_t savedViewID = g_CurrentViewID;
	g_CurrentViewID = VIEW_DEFERRED_SHADOW;

	render->Push3DView( *this, 0, m_pDummyTexture, GetFrustum(), m_pDepthTexture );
	SetupCurrentView( origin, angles, VIEW_DEFERRED_SHADOW );

	const bool bDrawEntities = ( nSetupFlags & DF_DRAW_ENTITITES ) != 0;
	BuildWorldRenderLists( bDrawEntities, iForceViewLeaf, ShouldCacheLists(), bShadowDepth, NULL );
	PruneWorldListInfo();
	if ( bDrawEntities )
	{
		BuildRenderableRenderLists( VIEW_SHADOW_DEPTH_TEXTURE );
	}

	render->PopView( GetFrustum() );

	g_CurrentViewID = savedViewID;
}

void CBaseShadowView::PushView( float waterHeight )
{
	render->Push3DView( *this, 0, m_pDummyTexture, GetFrustum(), m_pDepthTexture );
	SetupCurrentView( origin, angles, VIEW_DEFERRED_SHADOW );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( m_pDummyTexture, m_pDepthTexture, x, y, width, height );

#if defined( DEBUG ) || defined( SHADOWMAPPING_USE_COLOR )
	pRenderContext->ClearColor4ub( 255, 255, 255, 255 );
	pRenderContext->ClearBuffers( true, true );
#else
	pRenderContext->ClearBuffers( false, true );
#endif

	if ( m_bOutputRadiosity )
	{
		Assert( !IsErrorTexture( m_pRadAlbedoTexture ) );
		Assert( !IsErrorTexture( m_pRadNormalTexture ) );

		pRenderContext->SetRenderTargetEx( 1, m_pRadAlbedoTexture );
		pRenderContext->SetRenderTargetEx( 2, m_pRadNormalTexture );
	}
}

void CBaseShadowView::PopView()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PopRenderTargetAndViewport();

	render->PopView( GetFrustum() );
}

void CBaseShadowView::SetRadiosityOutputEnabled( bool bEnabled )
{
	m_bOutputRadiosity = bEnabled;
}

static void ComputeFrustumCorners( const CViewSetup &camView, float flNear, float flFar, Vector outCorners[8] )
{
	Vector camOrigin = camView.origin;
	Vector camFwd, camRight, camUp;
	AngleVectors( camView.angles, &camFwd, &camRight, &camUp );

	float flFov = ( camView.fov > 0.0f ) ? camView.fov : 75.0f;
	float flAspectRatio = camView.m_flAspectRatio;
	if ( flAspectRatio <= 0.0f )
	{
		if ( engine && engine->GetScreenAspectRatio() > 0.0f )
		{
			flAspectRatio = engine->GetScreenAspectRatio();
		}
		else if ( camView.width > 0 && camView.height > 0 )
		{
			flAspectRatio = ( (float)camView.width / (float)camView.height );
		}
		else
		{
			flAspectRatio = 4.0f / 3.0f;
		}
	}

	float flFovRad = DEG2RAD( flFov );
	float tanHalfFovX = tanf( flFovRad * 0.5f );
	float tanHalfFovY = tanHalfFovX / flAspectRatio;

	float nearW = flNear * tanHalfFovX;
	float nearH = flNear * tanHalfFovY;
	float farW  = flFar  * tanHalfFovX;
	float farH  = flFar  * tanHalfFovY;

	outCorners[0] = camOrigin + camFwd * flNear - camRight * nearW - camUp * nearH;
	outCorners[1] = camOrigin + camFwd * flNear + camRight * nearW - camUp * nearH;
	outCorners[2] = camOrigin + camFwd * flNear - camRight * nearW + camUp * nearH;
	outCorners[3] = camOrigin + camFwd * flNear + camRight * nearW + camUp * nearH;

	outCorners[4] = camOrigin + camFwd * flFar - camRight * farW - camUp * farH;
	outCorners[5] = camOrigin + camFwd * flFar + camRight * farW - camUp * farH;
	outCorners[6] = camOrigin + camFwd * flFar - camRight * farW + camUp * farH;
	outCorners[7] = camOrigin + camFwd * flFar + camRight * farW + camUp * farH;
}

lightData_Global_t& GetActiveGlobalLightState()
{
	static lightData_Global_t data;
	CLightingEditor *pEditor = GetLightingEditor();

	if ( pEditor->IsEditorLightingActive() && pEditor->GetKVGlobalLight() != NULL )
	{
		data = pEditor->GetGlobalState();
	}
	else if ( GetGlobalLight() != NULL )
	{
		data = GetGlobalLight()->GetState();
	}

	return data;
}

void CalculateCSMDistances( const CViewSetup &view, float &flSplitDist, float &flMaxDist )
{
	flMaxDist = 0.0f;
	flSplitDist = 0.0f;

	CDeferredLightGlobal *pGlobal = GetGlobalLight();
	if ( pGlobal != NULL )
	{
		flMaxDist = pGlobal->GetCSMMaxDist();
		flSplitDist = pGlobal->GetCSMSplitDist();
	}

	if ( flMaxDist <= 0.0f )
	{
		if ( view.zFar > 128.0f && view.zFar <= 4096.0f )
			flMaxDist = view.zFar;
		else
			flMaxDist = 4096.0f;
	}

	if ( flSplitDist <= 0.0f )
	{
		const float flNear = 16.0f;
		flSplitDist = 0.65f * sqrtf( flNear * flMaxDist ) + 0.35f * ( flNear + 0.5f * ( flMaxDist - flNear ) );
	}

	if ( flSplitDist <= 64.0f ) flSplitDist = 64.0f;
	if ( flMaxDist <= flSplitDist ) flMaxDist = flSplitDist * 4.0f;
}

void COrthoShadowView::CalcShadowView()
{
	const cascade_t &m_data = GetCascadeInfo( iCascadeIndex );

	float flMaxDist, flSplitDist;
	CalculateCSMDistances( m_CameraView, flSplitDist, flMaxDist );

	float flSplitFar = ( iCascadeIndex == 0 ) ? flSplitDist : flMaxDist;

	Vector frustumCorners[16];
	int numCorners = 8;
	ComputeFrustumCorners( m_CameraView, 0.0f, flSplitFar, frustumCorners );

	if ( m_bHasReflectionView )
	{
		ComputeFrustumCorners( m_ReflectionView, 0.0f, flSplitFar, &frustumCorners[8] );
		numCorners = 16;
	}

	Vector vMins = frustumCorners[0];
	Vector vMaxs = frustumCorners[0];
	for ( int i = 1; i < numCorners; i++ )
	{
		VectorMin( vMins, frustumCorners[i], vMins );
		VectorMax( vMaxs, frustumCorners[i], vMaxs );
	}
	Vector vFrustumCenter = ( vMins + vMaxs ) * 0.5f;

	float flMaxDistSqr = 0.0f;
	for ( int i = 0; i < numCorners; i++ )
	{
		float flDistSqr = ( frustumCorners[i] - vFrustumCenter ).LengthSqr();
		if ( flDistSqr > flMaxDistSqr )
			flMaxDistSqr = flDistSqr;
	}

	float flOrthoSize = ceilf( sqrtf( flMaxDistSqr ) * 2.0f * 1.05f );
	if ( flOrthoSize < 128.0f )
		flOrthoSize = 128.0f;

	const lightData_Global_t& state = GetActiveGlobalLightState();
	QAngle lightAng;
	VectorAngles( -state.vecLight.AsVector3D(), lightAng );

	Vector viewFwd, viewRight, viewUp;
	AngleVectors( lightAng, &viewFwd, &viewRight, &viewUp );

	float flLightSpaceX = DotProduct( vFrustumCenter, viewRight );
	float flLightSpaceY = DotProduct( vFrustumCenter, viewUp );

	float flWorldUnitsPerTexel = flOrthoSize / (float)m_data.iResolution;
	float flSnappedX = floorf( flLightSpaceX / flWorldUnitsPerTexel + 0.5f ) * flWorldUnitsPerTexel;
	float flSnappedY = floorf( flLightSpaceY / flWorldUnitsPerTexel + 0.5f ) * flWorldUnitsPerTexel;

	Vector vSnappedCenter = vFrustumCenter
		+ viewRight * ( flSnappedX - flLightSpaceX )
		+ viewUp * ( flSnappedY - flLightSpaceY );

	const float halfOrthoSize = flOrthoSize * 0.5f;

	origin = vSnappedCenter - viewFwd * m_data.flOriginOffset;
	angles = lightAng;

	x = 0;
	y = 0;
	height = m_data.iResolution;
	width = m_data.iResolution;

	m_bOrtho = true;
	m_OrthoLeft   = -halfOrthoSize;
	m_OrthoRight  =  halfOrthoSize;
	m_OrthoTop    = -halfOrthoSize;
	m_OrthoBottom =  halfOrthoSize;

	zNear = zNearViewmodel = 0.0f;
	zFar = zFarViewmodel = m_data.flOriginOffset + flOrthoSize * 1.5f;
	if ( zFar < m_data.flFarZ )
		zFar = zFarViewmodel = m_data.flFarZ;

	const float flCasterExtrudeDist = MAX( m_data.flOriginOffset, flMaxDist );
	m_CascadeCuller.Init( frustumCorners, viewFwd, flCasterExtrudeDist, m_bHasReflectionView ? &frustumCorners[8] : NULL );

	if ( r_deferred_csm_cull_debug.GetBool() )
	{
		Vector debugCorners[8];
		float flNearDebug = ( iCascadeIndex == 0 ) ? 16.0f : flSplitDist;
		ComputeFrustumCorners( m_CameraView, flNearDebug, flSplitFar, debugCorners );

		Vector vDirToLight = -viewFwd;
		Vector vExtruded[16];
		for ( int i = 0; i < 8; ++i )
		{
			vExtruded[i] = debugCorners[i];
			vExtruded[i + 8] = debugCorners[i] + vDirToLight * flCasterExtrudeDist;
		}

		static const int lines[32][2] = {
			{0,1}, {1,3}, {3,2}, {2,0},
			{4,5}, {5,7}, {7,6}, {6,4},
			{0,4}, {1,5}, {2,6}, {3,7},
			{0,8}, {1,9}, {2,10}, {3,11},
			{4,12}, {5,13}, {6,14}, {7,15},
			{8,9}, {9,11}, {11,10}, {10,8},
			{12,13}, {13,15}, {15,14}, {14,12},
			{8,12}, {9,13}, {10,14}, {11,15}
		};

		int r = ( iCascadeIndex == 0 ) ? 0 : 255;
		int g = ( iCascadeIndex == 0 ) ? 255 : 200;
		int b = ( iCascadeIndex == 0 ) ? 255 : 0;

		for ( int i = 0; i < 32; ++i )
		{
			debugoverlay->AddLineOverlay( vExtruded[lines[i][0]], vExtruded[lines[i][1]], r, g, b, true, -1.0f );
		}
	}

	m_flAspectRatio = 1.0f;

#if CSM_USE_COMPOSITED_TARGET
	x = m_data.iViewport_x;
	y = m_data.iViewport_y;
#endif
}

void COrthoShadowView::CommitData()
{
	struct sendShadowDataOrtho
	{
		shadowData_ortho_t data;
		int index;
		static void Fire( sendShadowDataOrtho d )
		{
			GetDeferredExt()->CommitShadowData_Ortho( d.index, d.data );
		};
	};

	const cascade_t &data = GetCascadeInfo( iCascadeIndex );

	Vector fwd, right, down;
	AngleVectors( angles, &fwd, &right, &down );
	down *= -1.0f;

	shadowData_ortho_t shadowData;

#if CSM_USE_COMPOSITED_TARGET
	shadowData.iRes_x = CSM_COMP_RES_X;
	shadowData.iRes_y = CSM_COMP_RES_Y;
#else
	shadowData.iRes_x = width;
	shadowData.iRes_y = height;
#endif

	shadowData.vecSlopeSettings.Init(
		data.flSlopeScaleMin, data.flSlopeScaleMax, data.flNormalScaleMax, 1.0f / zFar
		);
	shadowData.vecOrigin.Init( origin );

	Vector4D matrix_scale_offset( 0.5f, -0.5f, 0.5f, 0.5f );

#if CSM_USE_COMPOSITED_TARGET
	shadowData.vecUVTransform.Init( x / (float)CSM_COMP_RES_X,
		y / (float)CSM_COMP_RES_Y,
		width / (float) CSM_COMP_RES_X,
		height / (float) CSM_COMP_RES_Y );
#endif

	VMatrix a,b,c,d,screenToTexture;
	render->GetMatricesForView( *this, &a, &b, &c, &d );
	MatrixBuildScale( screenToTexture, matrix_scale_offset.x,
		matrix_scale_offset.y,
		1.0f );

	screenToTexture[0][3] = matrix_scale_offset.z;
	screenToTexture[1][3] = matrix_scale_offset.w;

	MatrixMultiply( screenToTexture, c, shadowData.matWorldToTexture );

	QUEUE_FIRE( CommitShadowData_Ortho, iCascadeIndex, shadowData );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX, iCascadeIndex );
}

void COrthoShadowView::DrawSetup( float waterHeight, int nSetupFlags, float waterZAdjust, int iForceViewLeaf, bool bShadowDepth )
{
	BaseClass::DrawSetup( waterHeight, nSetupFlags, waterZAdjust, iForceViewLeaf, bShadowDepth );

	if ( r_deferred_csm_cull_convex.GetBool() )
	{
		PruneShadowCasters();
	}
}

void COrthoShadowView::PruneShadowCasters()
{
	if ( !m_pRenderablesList )
		return;

	m_CascadeCuller.ResetStats();

	for ( int group = 0; group < RENDER_GROUP_COUNT; ++group )
	{
		int nCount = m_pRenderablesList->m_RenderGroupCounts[group];
		if ( nCount <= 0 )
			continue;

		CClientRenderablesList::CEntry *pEntries = m_pRenderablesList->m_RenderGroups[group];
		int nKept = 0;

		for ( int i = 0; i < nCount; ++i )
		{
			IClientRenderable *pRenderable = pEntries[i].m_pRenderable;
			if ( !pRenderable )
			{
				pEntries[nKept++] = pEntries[i];
				continue;
			}

			Vector absMins, absMaxs;
			pRenderable->GetRenderBoundsWorldspace( absMins, absMaxs );

			if ( m_CascadeCuller.CheckBox( absMins, absMaxs ) )
			{
				pEntries[nKept++] = pEntries[i];
			}
		}

		m_pRenderablesList->m_RenderGroupCounts[group] = nKept;
	}

	if ( iCascadeIndex >= 0 && iCascadeIndex < 2 )
	{
		CVolumeCuller::Stats_t stats = m_CascadeCuller.GetStats();
		g_CSMCullStats.nTested[iCascadeIndex] = stats.nTotalTested;
		g_CSMCullStats.nCulled[iCascadeIndex] = stats.nCulled;
	}
}

void CTetrahedralShadowView::CalcShadowView()
{
	static const Vector s_vecTetraForward[4] =
	{
		Vector( -0.57735027f, -0.57735027f, -0.57735027f ), // Face 0
		Vector(  0.57735027f,  0.57735027f, -0.57735027f ), // Face 1
		Vector(  0.57735027f, -0.57735027f,  0.57735027f ), // Face 2
		Vector( -0.57735027f,  0.57735027f,  0.57735027f )  // Face 3
	};

	static const Vector s_vecTetraUp[4] =
	{
		Vector( -0.40824829f, -0.40824829f,  0.81649658f ), // Face 0
		Vector(  0.40824829f,  0.40824829f,  0.81649658f ), // Face 1
		Vector(  0.40824829f, -0.40824829f, -0.81649658f ), // Face 2
		Vector( -0.40824829f,  0.40824829f, -0.81649658f )  // Face 3
	};

	Vector fwdWorld, upWorld;
	VectorRotate( s_vecTetraForward[m_iFaceIndex], m_pLight->ang, fwdWorld );
	VectorRotate( s_vecTetraUp[m_iFaceIndex], m_pLight->ang, upWorld );
	VectorAngles( fwdWorld, upWorld, angles );

	const int pointRes = ( m_pLight && m_pLight->IsStatic() && m_pLight->HasValidAtlasSlot() )
		? STATIC_POINT_SHADOW_TILE_RES
		: GetShadowResolution_Point();

	m_bOrtho = false;
	fov = fovViewmodel = 144.0f;
	m_flAspectRatio = 1.0f;

	width = pointRes;
	height = pointRes;

	x = ( m_iFaceIndex % 2 ) * pointRes;
	y = ( m_iFaceIndex / 2 ) * pointRes;

	zNear = zNearViewmodel = 2.0f;
	zFar = zFarViewmodel = m_pLight->flRadius;
}

void CTetrahedralShadowView::CommitData()
{
	Vector fwd;
	AngleVectors( angles, &fwd );

	shadowData_proj_t data;
	data.vecForward.Init( fwd );
	data.vecOrigin.Init( origin );
	data.vecSlopeSettings.Init( 0.001f, 0.005f, 3.0f, 1.0f / m_pLight->flRadius );

	QUEUE_FIRE( CommitShadowData_Proj, m_iIndex, data );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX, m_iIndex );
}

void CSpotLightShadowView::CalcShadowView()
{
	float flRadius = m_pLight->flRadius;

	int spotRes = ( m_pLight && m_pLight->IsStatic() && m_pLight->HasValidAtlasSlot() )
		? r_deferred_rt_shadowspot_static_res.GetInt()
		: GetShadowResolution_Spot();

	width = spotRes;
	height = spotRes;

	zNear = zNearViewmodel = DEFLIGHT_SPOT_ZNEAR;
	zFar = zFarViewmodel = flRadius;

	fov = fovViewmodel = m_pLight->GetFOV();
}

void CSpotLightShadowView::CommitData()
{
	struct sendShadowDataProj
	{
		shadowData_proj_t data;
		int index;
		static void Fire( sendShadowDataProj d )
		{
			GetDeferredExt()->CommitShadowData_Proj( d.index, d.data );
		};
	};

	Vector fwd;
	AngleVectors( angles, &fwd );

	shadowData_proj_t data;
	data.vecForward.Init( fwd );
	data.vecOrigin.Init( origin );
	// slope min, slope max, normal max, depth
	//data.data.vecSlopeSettings.Init( 0.005f, 0.02f, 3, zFar );
	data.vecSlopeSettings.Init( 0.001f, 0.005f, 3, 0 );

	QUEUE_FIRE( CommitShadowData_Proj, m_iIndex, data );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX, m_iIndex );
}


void CDeferredViewRender::ResetCascadeDelay()
{
	for ( int i = 0; i < SHADOW_NUM_CASCADES; i++ )
		m_flRenderDelay[i] = 0;
}


void CDeferredViewRender::RenderGlobalShadows( const CViewSetup &view, const CViewSetup *pReflView )
{
	if ( GetGlobalLight() == NULL )
		return;

	lightData_Global_t lightDataState = GetActiveGlobalLightState();
	CalculateCSMDistances( view, lightDataState.flCSMSplitDist, lightDataState.flCSMMaxDist );

	if ( !GetLightingEditor()->IsEditorLightingActive() &&
		r_deferred_light_global_override.GetBool() )
	{
		lightDataState.bShadow = r_deferred_light_global_override_shadow.GetBool();
		UTIL_StringToVector( lightDataState.diff.AsVector3D().Base(), r_deferred_light_global_override_diffuse.GetString() );
		UTIL_StringToVector( lightDataState.ambh.AsVector3D().Base(), r_deferred_light_global_override_ambient_high.GetString() );
		UTIL_StringToVector( lightDataState.ambl.AsVector3D().Base(), r_deferred_light_global_override_ambient_low.GetString() );

		lightDataState.bEnabled = ( lightDataState.diff.LengthSqr() > 0.000001f ||
			lightDataState.ambh.LengthSqr() > 0.000001f ||
			lightDataState.ambl.LengthSqr() > 0.000001f );
	}

	QUEUE_FIRE( CommitLightData_Global, lightDataState );

	if ( lightDataState.bEnabled && lightDataState.bShadow )
	{
#if DEFCFG_ENABLE_RADIOSITY
		const bool bRadiosityEnabled = r_deferred_radiosity.GetBool();
#else
		const bool bRadiosityEnabled = false;
#endif
		Vector origins[3];
		int numOrigins = 2;
		origins[0] = view.origin;
		origins[1] = view.origin + lightDataState.vecLight.AsVector3D() * 1024;
		if ( pReflView )
		{
			origins[2] = pReflView->origin;
			numOrigins = 3;
		}
		render->ViewSetupVis( false, numOrigins, origins );

		RenderCascadedShadows( view, bRadiosityEnabled, pReflView );
	}
}


void CDeferredViewRender::RenderCascadedShadows( const CViewSetup &view, const bool bEnableRadiosity, const CViewSetup *pReflView )
{
	for ( int i = 0; i < SHADOW_NUM_CASCADES; i++ )
	{
		if ( i == 0 )
		{
			g_CSMCullStats.nTested[0] = 0; g_CSMCullStats.nTested[1] = 0;
			g_CSMCullStats.nCulled[0] = 0; g_CSMCullStats.nCulled[1] = 0;
		}

		const cascade_t &cascade = GetCascadeInfo(i);
		const bool bDoRadiosity = bEnableRadiosity && cascade.bOutputRadiosityData;
		const int iRadTarget = cascade.iRadiosityCascadeTarget;

#if CSM_USE_COMPOSITED_TARGET == 0
		int textureIndex = i;
#else
		int textureIndex = 0;
#endif

		CRefPtr<COrthoShadowView> pOrthoDepth = new COrthoShadowView( this, i );
		pOrthoDepth->Setup( view, GetShadowDepthRT_Ortho( textureIndex ), GetShadowColorRT_Ortho( textureIndex ), pReflView );
		if ( bDoRadiosity )
		{
			pOrthoDepth->SetRadiosityOutputEnabled( true );
			pOrthoDepth->SetupRadiosityTargets( GetRadiosityAlbedoRT_Ortho( textureIndex ),
				GetRadiosityNormalRT_Ortho( textureIndex ) );
		}
		AddViewToScene( pOrthoDepth );

		if ( bDoRadiosity )
			PerformRadiosityGlobal( iRadTarget, view );
	}
}

void CDeferredViewRender::DrawLightShadowView( const CViewSetup &view, int iDesiredShadowmap, def_light_t *l )
{
	extern ConVar r_deferred_shadow_cache;
	if ( r_deferred_shadow_cache.GetBool() && l->IsStatic() && l->IsSpot() && l->HasValidAtlasSlot() )
	{
		if ( !engine->IsInGame() || engine->IsLevelMainMenuBackground() || engine->IsDrawingLoadingImage() )
			return;

		if ( !l->IsShadowDirty() )
		{
			g_nShadowViewsSkippedThisFrame++;
			return;
		}

		g_nShadowViewsRenderedThisFrame++;

		const int slot = l->m_nCachedAtlasSlot;

		CViewSetup setup;
		setup.origin = l->pos;
		setup.angles = l->ang;
		setup.m_bOrtho = false;
		setup.m_flAspectRatio = 1;
		setup.x = 0;
		setup.y = 0;
		const int nStaticRes = r_deferred_rt_shadowspot_static_res.GetInt();
		setup.width = nStaticRes;
		setup.height = nStaticRes;

		Vector origins[2] = { view.origin, l->pos };
		render->ViewSetupVis( false, 2, origins );

		CRefPtr<CSpotLightShadowView> pProjView = new CSpotLightShadowView( this, l, iDesiredShadowmap );
		pProjView->Setup( setup, GetShadowDepthRT_Proj( iDesiredShadowmap ), GetShadowColorRT_Static( slot ) );
		AddViewToScene( pProjView );

		l->SetShadowDirty( false );
		return;
	}

	if ( r_deferred_shadow_cache.GetBool() && l->IsStatic() && l->IsPoint() && l->HasValidAtlasSlot() )
	{
		if ( !engine->IsInGame() || engine->IsLevelMainMenuBackground() || engine->IsDrawingLoadingImage() )
			return;

		if ( !l->IsShadowDirty() )
		{
			g_nShadowViewsSkippedThisFrame += 4;
			return;
		}

		g_nShadowViewsRenderedThisFrame += 4;

		const int slot = l->m_nCachedAtlasSlot;

		CViewSetup setup;
		setup.origin = l->pos;
		setup.angles = l->ang;
		setup.m_bOrtho = false;
		setup.m_flAspectRatio = 1;
		setup.x = 0;
		setup.y = 0;
		setup.width = STATIC_POINT_SHADOW_TILE_RES;
		setup.height = STATIC_POINT_SHADOW_TILE_RES;

		Vector origins[2] = { view.origin, l->pos };
		render->ViewSetupVis( false, 2, origins );

		for ( int iFace = 0; iFace < 4; iFace++ )
		{
			CRefPtr<CTetrahedralShadowView> pTetraView = new CTetrahedralShadowView( this, l, iDesiredShadowmap, iFace );
			pTetraView->Setup( setup, GetShadowDepthRT_DP( iDesiredShadowmap ), GetShadowColorRT_StaticPoint( slot ) );
			AddViewToScene( pTetraView );
		}

		l->SetShadowDirty( false );
		return;
	}

	CViewSetup setup;
	setup.origin = l->pos;
	setup.angles = l->ang;
	setup.m_bOrtho = false;
	setup.m_flAspectRatio = 1;
	setup.x = setup.y = 0;
	setup.width = ( l->iLighttype == DEFLIGHTTYPE_SPOT ) ? GetShadowResolution_Spot() : GetShadowResolution_Point();
	setup.height = setup.width;

	Vector origins[2] = { view.origin, l->pos };
	render->ViewSetupVis( false, 2, origins );

	switch ( l->iLighttype )
	{
	default:
		Assert( 0 );
		break;
	case DEFLIGHTTYPE_POINT:
		{
			g_nShadowViewsRenderedThisFrame += 4;
			for ( int iFace = 0; iFace < 4; iFace++ )
			{
				CRefPtr<CTetrahedralShadowView> pTetraView = new CTetrahedralShadowView( this, l, iDesiredShadowmap, iFace );
				pTetraView->Setup( setup, GetShadowDepthRT_DP( iDesiredShadowmap ), GetShadowColorRT_DP( iDesiredShadowmap ) );
				AddViewToScene( pTetraView );
			}
		}
		break;
	case DEFLIGHTTYPE_SPOT:
		{
			g_nShadowViewsRenderedThisFrame++;
			CRefPtr<CSpotLightShadowView> pProjView = new CSpotLightShadowView( this, l, iDesiredShadowmap );

			pProjView->Setup( setup, GetShadowDepthRT_Proj( iDesiredShadowmap ), GetShadowColorRT_Proj( iDesiredShadowmap ) );
			AddViewToScene( pProjView );
		}
		break;
	}
}

