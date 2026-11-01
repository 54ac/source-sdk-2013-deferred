#include "cbase.h"
#include "deferred/viewrender_deferred_internal.h"
#include "tier0/memdbgon.h"

int g_nShadowViewsSkippedThisFrame = 0;
int g_nShadowViewsRenderedThisFrame = 0;
int g_nShadowViewsSkippedLastFrame = 0;
int g_nShadowViewsRenderedLastFrame = 0;

void DrawCube( CMeshBuilder &meshBuilder, Vector vecPos, float flRadius, float *pfl2Texcoords )
{
	const float flOffsets[24][3] = {
		-flRadius, -flRadius, -flRadius,
		flRadius, -flRadius, -flRadius,
		flRadius, flRadius, -flRadius,
		-flRadius, flRadius, -flRadius,

		flRadius, flRadius, flRadius,
		flRadius, flRadius, -flRadius,
		flRadius, -flRadius, -flRadius,
		flRadius, -flRadius, flRadius,

		-flRadius, flRadius, flRadius,
		-flRadius, -flRadius, flRadius,
		-flRadius, -flRadius, -flRadius,
		-flRadius, flRadius, -flRadius,

		flRadius, flRadius, flRadius,
		-flRadius, flRadius, flRadius,
		-flRadius, flRadius, -flRadius,
		flRadius, flRadius, -flRadius,

		flRadius, -flRadius, flRadius,
		flRadius, -flRadius, -flRadius,
		-flRadius, -flRadius, -flRadius,
		-flRadius, -flRadius, flRadius,

		flRadius, flRadius, flRadius,
		flRadius, -flRadius, flRadius,
		-flRadius, -flRadius, flRadius,
		-flRadius, flRadius, flRadius,
	};

	for ( int i = 0; i < 24; i++ )
	{
		meshBuilder.Position3f( vecPos.x + flOffsets[i][0],
			vecPos.y + flOffsets[i][1],
			vecPos.z + flOffsets[i][2] );
		meshBuilder.TexCoord2fv( 0, pfl2Texcoords );
		meshBuilder.AdvanceVertex();
	}
}



bool CBaseWorldViewDeferred::AdjustView( float waterHeight )
{
	if( m_DrawFlags & DF_RENDER_REFRACTION )
	{
		ITexture *pTexture = GetWaterRefractionTexture();

		// Use the aspect ratio of the main view! So, don't recompute it here
		x = y = 0;
		width = pTexture->GetActualWidth();
		height = pTexture->GetActualHeight();

		return true;
	}

	if( m_DrawFlags & DF_RENDER_REFLECTION )
	{
		ITexture *pTexture = GetWaterReflectionTexture();

		// Use the aspect ratio of the main view! So, don't recompute it here
		x = y = 0;
		width = pTexture->GetActualWidth();
		height = pTexture->GetActualHeight();
		angles[0] = -angles[0];
		angles[2] = -angles[2];
		origin[2] -= 2.0f * ( origin[2] - (waterHeight));
		return true;
	}

	return false;
}

void CBaseWorldViewDeferred::DrawSetup( float waterHeight, int nSetupFlags, float waterZAdjust, int iForceViewLeaf,
	bool bShadowDepth )
{
	const view_id_t savedViewID = g_CurrentViewID;
	g_CurrentViewID = bShadowDepth ? VIEW_DEFERRED_SHADOW : VIEW_MAIN;

	const bool bViewChanged = AdjustView( waterHeight );

	if ( bViewChanged )
	{
		render->Push3DView( *this, 0, NULL, GetFrustum() );
		SetupCurrentView( origin, angles, (view_id_t)savedViewID );
	}

	const bool bDrawEntities = ( nSetupFlags & DF_DRAW_ENTITITES ) != 0;
	const bool bDrawReflection = ( ( nSetupFlags & DF_RENDER_REFLECTION ) != 0 ) || ( ( nSetupFlags & DF_CLIP_Z ) != 0 );
	BuildWorldRenderLists( bDrawEntities, iForceViewLeaf, ShouldCacheLists(), false, bDrawReflection ? &waterHeight : NULL );

	PruneWorldListInfo();

	if ( bDrawEntities )
	{
		const bool bOptimized = bShadowDepth || savedViewID == VIEW_DEFERRED_GBUFFER;
		BuildRenderableRenderLists( bOptimized ? VIEW_SHADOW_DEPTH_TEXTURE : savedViewID );
	}

	if ( bViewChanged )
	{
		render->PopView( GetFrustum() );
	}

	g_CurrentViewID = savedViewID;
}

void CBaseWorldViewDeferred::DrawExecute( float waterHeight, view_id_t viewID, float waterZAdjust, bool bShadowDepth )
{
	const view_id_t savedViewID = g_CurrentViewID;

	// @MULTICORE (toml 8/16/2006): rethink how, where, and when this is done...
	g_CurrentViewID = VIEW_SHADOW_DEPTH_TEXTURE;
	//MaybeInvalidateLocalPlayerAnimation();
	//g_pClientShadowMgr->ComputeShadowTextures( *this, m_pWorldListInfo->m_LeafCount, m_pWorldListInfo->m_pLeafList );
	MaybeInvalidateLocalPlayerAnimation();

	// Make sure sound doesn't stutter
	engine->Sound_ExtraUpdate();

	g_CurrentViewID = viewID;

	// Update our render view flags.
	const int iDrawFlagsBackup = m_DrawFlags;
	m_DrawFlags |= m_pMainView->GetBaseDrawFlags();

	PushView( waterHeight );

	CMatRenderContextPtr pRenderContext( materials );

	int iDeferredViewID = DEFERRED_VIEW_MAIN;
	if ( viewID == VIEW_REFRACTION )
		iDeferredViewID = DEFERRED_VIEW_REFRACTION;
	else if ( viewID == VIEW_REFLECTION )
		iDeferredViewID = DEFERRED_VIEW_REFLECTION;

	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_VIEW_ID, iDeferredViewID );

	ITexture *pSaveFrameBufferCopyTexture = pRenderContext->GetFrameBufferCopyTexture( 0 );
	if ( engine->GetDXSupportLevel() >= 80 )
	{
		pRenderContext->SetFrameBufferCopyTexture( GetPowerOfTwoFrameBufferTexture() );
	}

	pRenderContext.SafeRelease();

	//helper::bDisableDecalRendering = m_bDrawWorldNormal;

	DrawWorldDeferred( waterZAdjust );

	const bool bIsGBufferOrShadow = bShadowDepth || ( viewID == VIEW_DEFERRED_GBUFFER );

	//if ( m_DrawFlags & DF_DRAW_ENTITITES )
	DrawOpaqueRenderablesDeferred( bShadowDepth );

	if ( !bIsGBufferOrShadow )
	{
		if ( m_DrawFlags & DF_DRAW_ENTITITES )
		{
			DrawTranslucentRenderables( false, false );
			if (!bShadowDepth)
				DrawNoZBufferTranslucentRenderables();
		}
		else
		{
			// Draw translucent world brushes only, no entities
			DrawTranslucentWorldInLeaves( false );
		}
	}

	pRenderContext.GetFrom( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_VIEW_ID, DEFERRED_VIEW_NONE );
	pRenderContext->SetFrameBufferCopyTexture( pSaveFrameBufferCopyTexture );
	PopView();

	m_DrawFlags = iDrawFlagsBackup;

	g_CurrentViewID = savedViewID;
	//helper::bDisableDecalRendering = false;
}

void CBaseWorldViewDeferred::PushView( float waterHeight )
{
	float spread = 4.0f;
	if( m_DrawFlags & DF_FUDGE_UP )
	{
		waterHeight += spread;
	}
	else
	{
		waterHeight -= spread;
	}

	MaterialHeightClipMode_t clipMode = MATERIAL_HEIGHTCLIPMODE_DISABLE;
	if ( ( m_DrawFlags & DF_CLIP_Z ) && mat_clipz.GetBool() )
	{
		if( m_DrawFlags & DF_CLIP_BELOW )
		{
			clipMode = MATERIAL_HEIGHTCLIPMODE_RENDER_ABOVE_HEIGHT;
		}
		else
		{
			clipMode = MATERIAL_HEIGHTCLIPMODE_RENDER_BELOW_HEIGHT;
		}
	}

	CMatRenderContextPtr pRenderContext( materials );

	if( m_DrawFlags & DF_RENDER_REFRACTION )
	{
		pRenderContext->SetFogZ( waterHeight );
		pRenderContext->SetHeightClipZ( waterHeight );
		pRenderContext->SetHeightClipMode( clipMode );

		// Have to re-set up the view since we reset the size
		render->Push3DView( *this, m_ClearFlags, GetWaterRefractionTexture(), GetFrustum() );

		return;
	}

	if( m_DrawFlags & DF_RENDER_REFLECTION )
	{
		ITexture *pTexture = GetWaterReflectionTexture();

		pRenderContext->SetFogZ( waterHeight );

		bool bSoftwareUserClipPlane = g_pMaterialSystemHardwareConfig->UseFastClipping();
		if( bSoftwareUserClipPlane && ( origin[2] > waterHeight - r_eyewaterepsilon.GetFloat() ) )
		{
			waterHeight = origin[2] + r_eyewaterepsilon.GetFloat();
		}

		pRenderContext->SetHeightClipZ( waterHeight );
		pRenderContext->SetHeightClipMode( clipMode );

		render->Push3DView( *this, m_ClearFlags, pTexture, GetFrustum() );
		return;
	}

	if ( m_ClearFlags & ( VIEW_CLEAR_DEPTH | VIEW_CLEAR_COLOR | VIEW_CLEAR_STENCIL ) )
	{
		if ( m_ClearFlags & VIEW_CLEAR_OBEY_STENCIL )
		{
			pRenderContext->ClearBuffersObeyStencil( m_ClearFlags & VIEW_CLEAR_COLOR, m_ClearFlags & VIEW_CLEAR_DEPTH );
		}
		else
		{
			pRenderContext->ClearBuffers( m_ClearFlags & VIEW_CLEAR_COLOR, m_ClearFlags & VIEW_CLEAR_DEPTH, m_ClearFlags & VIEW_CLEAR_STENCIL );
		}
	}

	pRenderContext->SetHeightClipMode( clipMode );
	if ( clipMode != MATERIAL_HEIGHTCLIPMODE_DISABLE )
	{
		pRenderContext->SetHeightClipZ( waterHeight );
	}
}

void CBaseWorldViewDeferred::PopView()
{
	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->SetHeightClipMode( MATERIAL_HEIGHTCLIPMODE_DISABLE );
	if( m_DrawFlags & (DF_RENDER_REFRACTION | DF_RENDER_REFLECTION) )
	{
		if ( IsX360() )
		{
			// these renders paths used their surfaces, so blit their results
			if ( m_DrawFlags & DF_RENDER_REFRACTION )
			{
				pRenderContext->CopyRenderTargetToTextureEx( GetWaterRefractionTexture(), NULL, NULL );
			}
			if ( m_DrawFlags & DF_RENDER_REFLECTION )
			{
				pRenderContext->CopyRenderTargetToTextureEx( GetWaterReflectionTexture(), NULL, NULL );
			}
		}

		render->PopView( GetFrustum() );
	}
}

void CBaseWorldViewDeferred::DrawWorldDeferred( float waterZAdjust )
{
	DrawWorld( waterZAdjust );
}

void CBaseWorldViewDeferred::DrawOpaqueRenderablesDeferred(bool k)
{
	DrawOpaqueRenderables(k ? DEPTH_MODE_SHADOW : DEPTH_MODE_NORMAL);
}

void CBaseWorldViewDeferred::PushComposite()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_COMPOSITION );
}

void CBaseWorldViewDeferred::PopComposite()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_INVALID );
}

//-----------------------------------------------------------------------------
// Draws the scene when there's no water or only cheap water
//-----------------------------------------------------------------------------
void CSimpleWorldViewDeferred::Setup( const CViewSetup &view, int nClearFlags, bool bDrawSkybox,
	const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t &waterInfo, ViewCustomVisibility_t *pCustomVisibility )
{
	BaseClass::Setup( view );

	m_ClearFlags = nClearFlags;
	m_DrawFlags = DF_DRAW_ENTITITES;

	if ( !waterInfo.m_bOpaqueWater )
	{
		m_DrawFlags |= DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
	}
	else
	{
		const bool bViewIntersectsWater = DoesViewPlaneIntersectWater( fogInfo.m_flWaterHeight, fogInfo.m_nVisibleFogVolume );
		if( bViewIntersectsWater )
		{
			// have to draw both sides if we can see both.
			m_DrawFlags |= DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
		}
		else if ( fogInfo.m_bEyeInFogVolume )
		{
			m_DrawFlags |= DF_RENDER_UNDERWATER;
		}
		else
		{
			m_DrawFlags |= DF_RENDER_ABOVEWATER;
		}
	}
	if ( waterInfo.m_bDrawWaterSurface )
	{
		m_DrawFlags |= DF_RENDER_WATER;
	}

	if ( !fogInfo.m_bEyeInFogVolume && bDrawSkybox )
	{
		m_DrawFlags |= DF_DRAWSKYBOX;
	}

	m_pCustomVisibility = pCustomVisibility;
	m_fogInfo = fogInfo;
}

void CSimpleWorldViewDeferred::Draw()
{
	VPROF( "CViewRender::ViewDrawScene_NoWater" );

	CMatRenderContextPtr pRenderContext( materials );
	PIXEVENT( pRenderContext, "CSimpleWorldView::Draw" );

	pRenderContext.SafeRelease();

	PushComposite();

	DrawSetup( 0, m_DrawFlags, 0 );

	if ( !m_fogInfo.m_bEyeInFogVolume )
	{
		EnableWorldFog();
	}
	else
	{
		m_ClearFlags |= VIEW_CLEAR_COLOR;

		SetFogVolumeState( m_fogInfo, false );

		pRenderContext.GetFrom( materials );

		unsigned char ucFogColor[3];
		pRenderContext->GetFogColor( ucFogColor );
		pRenderContext->ClearColor4ub( ucFogColor[0], ucFogColor[1], ucFogColor[2], 255 );
	}

	pRenderContext.SafeRelease();

	DrawExecute( 0, CurrentViewID(), 0 );

	PopComposite();

	pRenderContext.GetFrom( materials );
	pRenderContext->ClearColor4ub( 0, 0, 0, 255 );
}

void CGBufferView::Setup( const CViewSetup &view, bool bDrewSkybox, float flWaterHeight, bool bClipWater, bool bDrawEntities )
{
	m_fogInfo.m_bEyeInFogVolume = false;
	m_bDrewSkybox = bDrewSkybox;
	m_waterHeight = flWaterHeight;

	BaseClass::Setup( view );

	m_ClearFlags = 0;
	m_DrawFlags = DF_RENDER_ABOVEWATER;
	if ( bDrawEntities )
	{
		m_DrawFlags |= DF_DRAW_ENTITITES;
	}

	if ( bClipWater )
	{
		m_DrawFlags |= DF_CLIP_Z | DF_CLIP_BELOW;
	}
	else
	{
		m_DrawFlags |= DF_RENDER_UNDERWATER;
	}

#if DEFCFG_DEFERRED_SHADING
	if ( !bDrewSkybox )
		m_DrawFlags |= DF_DRAWSKYBOX;
#endif
}

void CGBufferView::Draw()
{
	VPROF( "CViewRender::ViewDrawScene_NoWater" );

	CMatRenderContextPtr pRenderContext( materials );
	PIXEVENT( pRenderContext, "CSimpleWorldViewDeferred::Draw" );

#if defined( _X360 )
	pRenderContext->PushVertexShaderGPRAllocation( 32 ); //lean toward pixel shader threads
#endif

	SetupCurrentView( origin, angles, VIEW_DEFERRED_GBUFFER );

	DrawSetup( m_waterHeight, m_DrawFlags, 0 );

	const bool bOptimizedGbuffer = DEFCFG_DEFERRED_SHADING == 0;
	DrawExecute( m_waterHeight, CurrentViewID(), 0, bOptimizedGbuffer );

	pRenderContext.GetFrom( materials );
	pRenderContext->ClearColor4ub( 0, 0, 0, 255 );

#if defined( _X360 )
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}

void CGBufferView::PushView( float waterHeight )
{
	float spread = 4.0f;
	if ( m_DrawFlags & DF_FUDGE_UP )
	{
		waterHeight += spread;
	}
	else
	{
		waterHeight -= spread;
	}

	MaterialHeightClipMode_t clipMode = MATERIAL_HEIGHTCLIPMODE_DISABLE;
	if ( ( m_DrawFlags & DF_CLIP_Z ) && mat_clipz.GetBool() )
	{
		if ( m_DrawFlags & DF_CLIP_BELOW )
		{
			clipMode = MATERIAL_HEIGHTCLIPMODE_RENDER_ABOVE_HEIGHT;
		}
		else
		{
			clipMode = MATERIAL_HEIGHTCLIPMODE_RENDER_BELOW_HEIGHT;
		}
	}

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetHeightClipZ( waterHeight );
	pRenderContext->SetHeightClipMode( clipMode );

	render->Push3DView( *this, 0, NULL, GetFrustum() );

	PushGBuffer( !m_bDrewSkybox, 1.0f, true, x, y, width, height );
}

void CGBufferView::PopView()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetHeightClipMode( MATERIAL_HEIGHTCLIPMODE_DISABLE );

	PopGBuffer();

	render->PopView( GetFrustum() );
}

void CGBufferView::PushGBuffer( bool bInitial, float zScale, bool bClearDepth, int vx, int vy, int vw, int vh )
{
	ITexture *pNormals = GetDefRT_Normals();
	ITexture *pDepth = GetDefRT_Depth();

	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->ClearColor4ub( 0, 0, 0, 0 );

#if DEFCFG_DEFERRED_SHADING == 1
	ITexture *pColorRT = GetDefRT_Albedo();
#else
	ITexture *pColorRT = pNormals;
#endif

	if ( vw <= 0 || vh <= 0 )
	{
		vx = vy = 0;
		vw = pColorRT->GetActualWidth();
		vh = pColorRT->GetActualHeight();
	}

	if ( bInitial )
	{
		pRenderContext->PushRenderTargetAndViewport( pDepth, NULL, vx, vy, vw, vh );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
	}

	pRenderContext->PushRenderTargetAndViewport( pColorRT, NULL, vx, vy, vw, vh );

	if ( bInitial )
		pRenderContext->ClearBuffers( true, false );

	if ( bClearDepth )
		pRenderContext->ClearBuffers( false, true );

	pRenderContext->SetRenderTargetEx( 1, pDepth );

#if DEFCFG_DEFERRED_SHADING == 1
	pRenderContext->SetRenderTargetEx( 2, pNormals );
	pRenderContext->SetRenderTargetEx( 3, GetDefRT_Specular() );
#elif !DEFCFG_LIGHTCTRL_PACKING
	pRenderContext->SetRenderTargetEx( 2, GetDefRT_LightCtrl() );
#endif

	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_GBUFFER );

	struct defData_setZScale
	{
	public:
		float zScale;

		static void Fire( defData_setZScale d )
		{
			GetDeferredExt()->CommitZScale( d.zScale );
		};
	};

	defData_setZScale data;
	data.zScale = zScale;
	#pragma warning(suppress: 4456)
	QUEUE_FIRE( CommitZScale, zScale );

	GetDeferredManager()->SetGBufferActive( true );
}

void CGBufferView::PopGBuffer()
{
	GetDeferredManager()->SetGBufferActive( false );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_INVALID );

	pRenderContext->PopRenderTargetAndViewport();
}

SkyboxVisibility_t CSkyboxViewDeferred::ComputeSkyboxVisibility()
{
	if ( ( enginetrace->GetPointContents( origin ) & CONTENTS_SOLID ) != 0 )
		return SKYBOX_NOT_VISIBLE;

	return engine->IsSkyboxVisibleFromPoint( origin );
}

bool CSkyboxViewDeferred::GetSkyboxFogEnable()
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return false;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	if( fog_override.GetInt() )
	{
		return fog_enableskybox.GetBool();
	}
	else
	{
		return !!local->m_skybox3d.fog.enable;
	}
}

void CSkyboxViewDeferred::Enable3dSkyboxFog( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return;
	}
	CPlayerLocalData *local = &pbp->m_Local;

	CMatRenderContextPtr pRenderContext( materials );

	if( GetSkyboxFogEnable() )
	{
		float fogColor[3];
		GetSkyboxFogColor( fogColor );
		float scale = 1.0f;
		if ( local->m_skybox3d.scale > 0.0f )
		{
			scale = 1.0f / local->m_skybox3d.scale;
		}
		pRenderContext->FogMode( MATERIAL_FOG_LINEAR );
		pRenderContext->FogColor3fv( fogColor );
		pRenderContext->FogStart( GetSkyboxFogStart() * scale );
		pRenderContext->FogEnd( GetSkyboxFogEnd() * scale );
		pRenderContext->FogMaxDensity( GetSkyboxFogMaxDensity() );
	}
	else
	{
		pRenderContext->FogMode( MATERIAL_FOG_NONE );
	}
}

sky3dparams_t *CSkyboxViewDeferred::PreRender3dSkyboxWorld( SkyboxVisibility_t nSkyboxVisible )
{
	if ( ( nSkyboxVisible != SKYBOX_3DSKYBOX_VISIBLE ) && r_3dsky.GetInt() != 2 )
		return NULL;

	// render the 3D skybox
	if ( !r_3dsky.GetInt() )
		return NULL;

	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();

	// No local player object yet...
	if ( !pbp )
		return NULL;

	CPlayerLocalData* local = &pbp->m_Local;
	if ( local->m_skybox3d.area == 255 )
		return NULL;

	return &local->m_skybox3d;
}

void CSkyboxViewDeferred::DrawInternal( view_id_t iSkyBoxViewID, ITexture *pRenderTarget, ITexture *pDepthTarget )
{
	const bool bInvokePreAndPostRender = !m_bGBufferPass;

	if ( m_bGBufferPass )
	{
#if DEFCFG_DEFERRED_SHADING
		m_DrawFlags |= DF_DRAWSKYBOX;
#endif
	}

	unsigned char **areabits = render->GetAreaBits();
	unsigned char *savebits;
	unsigned char tmpbits[ 32 ];
	savebits = *areabits;
	memset( tmpbits, 0, sizeof(tmpbits) );

	// set the sky area bit
	tmpbits[m_pSky3dParams->area>>3] |= 1 << (m_pSky3dParams->area&7);

	*areabits = tmpbits;

	// if you can get really close to the skybox geometry it's possible that you'll be able to clip into it
	// with this near plane.  If so, move it in a bit.  It's at 2.0 to give us more precision.  That means you
	// need to keep the eye position at least 2 * scale away from the geometry in the skybox
	zNear = 2.0;
	zFar = 10000.0f; //MAX_TRACE_LENGTH;

	float skyScale = 1.0f;
	// scale origin by sky scale and translate to sky origin
	{
		skyScale = (m_pSky3dParams->scale > 0) ? m_pSky3dParams->scale : 1.0f;
		const float scale = 1.0f / skyScale;

		const Vector& vSkyOrigin = m_pSky3dParams->origin;
		VectorScale( origin, scale, origin );
		VectorAdd( origin, vSkyOrigin, origin );
	}

	if ( !m_bGBufferPass )
		Enable3dSkyboxFog();

	// BUGBUG: Fix this!!!  We shouldn't need to call setup vis for the sky if we're connecting
	// the areas.  We'd have to mark all the clusters in the skybox area in the PVS of any
	// cluster with sky.  Then we could just connect the areas to do our vis.
	//m_bOverrideVisOrigin could hose us here, so call direct
	render->ViewSetupVis( false, 1, &m_pSky3dParams->origin.Get() );
	render->Push3DView( (*this), m_ClearFlags, pRenderTarget, GetFrustum(), pDepthTarget );

	if ( m_bGBufferPass )
		PushGBuffer( true, skyScale );
	else
		PushComposite();

	// Store off view origin and angles
	SetupCurrentView( origin, angles, iSkyBoxViewID );

#if defined( _X360 )
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushVertexShaderGPRAllocation( 32 );
	pRenderContext.SafeRelease();
#endif

	// Invoke pre-render methods
	if ( bInvokePreAndPostRender )
	{
		IGameSystem::PreRenderAllSystems();
	}

	BuildWorldRenderLists( true, -1, ShouldCacheLists() );

	BuildRenderableRenderLists( m_bGBufferPass ? VIEW_SHADOW_DEPTH_TEXTURE : iSkyBoxViewID );

	DrawWorldDeferred( 0.0f );

	// Iterate over all leaves and render objects in those leaves
	DrawOpaqueRenderablesDeferred( false );

	if ( !m_bGBufferPass )
	{
		// Iterate over all leaves and render objects in those leaves
		DrawTranslucentRenderables( true, false );
		DrawNoZBufferTranslucentRenderables();
	}

	if ( !m_bGBufferPass )
	{
		m_pMainView->DisableFog();

		CGlowOverlay::UpdateSkyOverlays( zFar, m_bCacheFullSceneState );

		PixelVisibility_EndCurrentView();
	}

	// restore old area bits
	*areabits = savebits;

	// Invoke post-render methods
	if( bInvokePreAndPostRender )
	{
		IGameSystem::PostRenderAllSystems();
		FinishCurrentView();
	}

	if ( m_bGBufferPass )
		PopGBuffer();
	else
		PopComposite();

	render->PopView( GetFrustum() );

#if defined( _X360 )
	pRenderContext.GetFrom( materials );
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}

bool CSkyboxViewDeferred::Setup( const CViewSetup &view, bool bGBuffer, SkyboxVisibility_t *pSkyboxVisible )
{
	BaseClass::Setup( view );

	// The skybox might not be visible from here
	*pSkyboxVisible = ComputeSkyboxVisibility();
	m_pSky3dParams = PreRender3dSkyboxWorld( *pSkyboxVisible );

	if ( !m_pSky3dParams )
	{
		return false;
	}

	m_bGBufferPass = bGBuffer;
	// At this point, we've cleared everything we need to clear
	// The next path will need to clear depth, though.
	m_ClearFlags = VIEW_CLEAR_DEPTH; //*pClearFlags;
	//*pClearFlags &= ~( VIEW_CLEAR_COLOR | VIEW_CLEAR_DEPTH | VIEW_CLEAR_STENCIL | VIEW_CLEAR_FULL_TARGET );
	//*pClearFlags |= VIEW_CLEAR_DEPTH; // Need to clear depth after rednering the skybox

	m_DrawFlags = DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER | DF_RENDER_WATER;
	if( !m_bGBufferPass && r_skybox.GetBool() )
	{
		m_DrawFlags |= DF_DRAWSKYBOX;
	}

	return true;
}

void CSkyboxViewDeferred::Draw()
{
	VPROF_BUDGET( "CViewRender::Draw3dSkyboxworld", "3D Skybox" );

	ITexture *pRTColor = NULL;
	ITexture *pRTDepth = NULL;
	if( m_eStereoEye != STEREO_EYE_MONO )
	{
		pRTColor = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(m_eStereoEye-1), ISourceVirtualReality::RT_Color );
		pRTDepth = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(m_eStereoEye-1), ISourceVirtualReality::RT_Depth );
	}

	DrawInternal(VIEW_3DSKY, pRTColor, pRTDepth);
}

void CPostLightingView::Setup( const CViewSetup &view )
{
	m_fogInfo.m_bEyeInFogVolume = false;

	BaseClass::Setup( view );

	m_ClearFlags = 0;

	m_DrawFlags = DF_DRAW_ENTITITES;
	m_DrawFlags |= DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
}

void CPostLightingView::Draw()
{
	VPROF( "CViewRender::ViewDrawScene_NoWater" );

	CMatRenderContextPtr pRenderContext( materials );
	PIXEVENT( pRenderContext, "CSimpleWorldViewDeferred::Draw" );

#if defined( _X360 )
	pRenderContext->PushVertexShaderGPRAllocation( 32 ); //lean toward pixel shader threads
#endif

	ITexture *pTexAlbedo = GetDefRT_Albedo();
	pRenderContext->CopyRenderTargetToTexture( pTexAlbedo );
	pRenderContext->PushRenderTargetAndViewport( pTexAlbedo );
	pRenderContext.SafeRelease();
	PushComposite();

	SetupCurrentView( origin, angles, VIEW_MAIN );

	DrawSetup( 0, m_DrawFlags, 0 );

	DrawExecute( 0, CurrentViewID(), 0, false );

	PopComposite();

	pRenderContext.GetFrom( materials );
	pRenderContext->PopRenderTargetAndViewport();

#if defined( _X360 )
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}

void CPostLightingView::PushView( float waterHeight )
{
	//PushGBuffer( !m_bDrewSkybox );
	BaseClass::PushView( waterHeight );
}

void CPostLightingView::PopView()
{
	BaseClass::PopView();
}

void CPostLightingView::DrawWorldDeferred( float waterZAdjust )
{
#if 0
	int iOldDrawFlags = m_DrawFlags;

	m_DrawFlags &= ~DF_DRAW_ENTITITES;
	m_DrawFlags &= ~DF_RENDER_UNDERWATER;
	m_DrawFlags &= ~DF_RENDER_ABOVEWATER;
	m_DrawFlags &= ~DF_DRAW_ENTITITES;

	BaseClass::DrawWorldDeferred( waterZAdjust );

	m_DrawFlags = iOldDrawFlags;
#endif
}

void CPostLightingView::DrawOpaqueRenderablesDeferred(bool)
{
}

void CPostLightingView::PushDeferredShadingFrameBuffer()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( GetDefRT_Albedo() );
}

void CPostLightingView::PopDeferredShadingFrameBuffer()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PopRenderTargetAndViewport();
}


CDeferredViewRender::CDeferredViewRender()
{
	m_pMesh_RadiosityScreenGrid[0] = NULL;
	m_pMesh_RadiosityScreenGrid[1] = NULL;
}

void CDeferredViewRender::Shutdown()
{
	CMatRenderContextPtr pRenderContext( materials );
	for ( IMesh* &mesh : m_pMesh_RadiosityScreenGrid)
	{
		if ( mesh != NULL )
			pRenderContext->DestroyStaticMesh( mesh );

		mesh = NULL;
	}

	for ( CUtlVector<IMesh*>& list : m_hRadiosityDebugMeshList )
	{
        const int count = list.Count();
		for ( int i = 0; i < count; i++ )
		{
            IMesh* mesh = list[i];
			Assert( mesh != NULL );

			pRenderContext->DestroyStaticMesh( mesh );
		}
	}

	BaseClass::Shutdown();
}

void CDeferredViewRender::LevelInit()
{
	BaseClass::LevelInit();

	ResetCascadeDelay();
}


void CDeferredViewRender::ViewDrawSceneDeferred( const CViewSetup &view, int nClearFlags, view_id_t viewID, bool bDrawViewModel )
{
	VPROF( "CViewRender::ViewDrawScene" );

	VisibleFogVolumeInfo_t fogVolumeInfo;
	render->GetVisibleFogVolume( view.origin, &fogVolumeInfo );

	WaterRenderInfo_t waterInfo;
	DetermineWaterRenderInfo( fogVolumeInfo, waterInfo );

	static ConVarRef r_WaterDrawReflection( "r_WaterDrawReflection" );
	const bool bNeedReflection = !fogVolumeInfo.m_bEyeInFogVolume &&
								 !waterInfo.m_bCheapWater &&
								 waterInfo.m_bReflect &&
								 r_WaterDrawReflection.GetBool();

	CViewSetup reflView;
	float flWaterHeight = 0.0f;

	if ( bNeedReflection )
	{
		reflView = view;
		flWaterHeight = fogVolumeInfo.m_flWaterHeight;
		ITexture *pReflectTex = GetWaterReflectionTexture();

		reflView.x = reflView.y = 0;
		reflView.width = pReflectTex->GetActualWidth();
		reflView.height = pReflectTex->GetActualHeight();
		reflView.angles[0] = -reflView.angles[0];
		reflView.angles[2] = -reflView.angles[2];
		reflView.origin[2] -= 2.0f * ( reflView.origin[2] - flWaterHeight );
	}

	RenderGlobalShadows( view, bNeedReflection ? &reflView : NULL );

	if ( bNeedReflection )
	{
		ProcessDeferredGlobals( reflView );
		GetLightingManager()->LightTearDown();
		GetLightingManager()->LightSetup( reflView );

		bool bDrew3dSkyboxRefl = false;
		SkyboxVisibility_t nSkyboxVisibleRefl = SKYBOX_NOT_VISIBLE;
		ViewDrawGBuffer( reflView, bDrew3dSkyboxRefl, nSkyboxVisibleRefl, false, flWaterHeight, true, waterInfo.m_bReflectEntities );

		PerformLighting( reflView );

		CAboveWaterViewDeferred::CReflectionView reflViewDeferred( this );
		reflViewDeferred.Setup( view, flWaterHeight, waterInfo.m_bReflectEntities );
		reflViewDeferred.Draw();

		ProcessDeferredGlobals( view );
		GetLightingManager()->LightTearDown();
		GetLightingManager()->LightSetup( view );
	}

	bool bDrew3dSkybox = false;
	SkyboxVisibility_t nSkyboxVisible = SKYBOX_NOT_VISIBLE;

	ViewDrawGBuffer( view, bDrew3dSkybox, nSkyboxVisible, bDrawViewModel );

	PerformLighting( view );

#if DEFCFG_DEFERRED_SHADING
	ViewCombineDeferredShading( view, viewID );
#else
	ViewDrawComposite( view, bDrew3dSkybox, nSkyboxVisible, nClearFlags, viewID, bDrawViewModel );
#endif

#if DEFCFG_ENABLE_RADIOSITY
	if ( r_deferred_radiosity_nodes.GetBool() )
		DebugRadiosity( view );
#endif

#if DEFCFG_DEFERRED_SHADING == 1
	CPostLightingView::PushDeferredShadingFrameBuffer();
#endif

	#ifdef SHADEREDITOR
	g_ShaderEditorSystem->UpdateSkymask( bDrew3dSkybox, view.x, view.y, view.width, view.height );
	#endif

	GetLightingManager()->RenderVolumetrics( view );

	// Disable fog for the rest of the stuff
	DisableFog();

	// UNDONE: Don't do this with masked brush models, they should probably be in a separate list
	// render->DrawMaskEntities()

	// Here are the overlays...
	CGlowOverlay::DrawOverlays( view.m_bCacheFullSceneState );

	// issue the pixel visibility tests
	PixelVisibility_EndCurrentView();

	// Draw rain..
	DrawPrecipitation();

	// Make sure sound doesn't stutter
	engine->Sound_ExtraUpdate();

	// Debugging info goes over the top
	CDebugViewRender::Draw3DDebuggingInfo( view );

	// Draw client side effects
	// NOTE: These are not sorted against the rest of the frame
	clienteffects->DrawEffects( gpGlobals->frametime );

	// Mark the frame as locked down for client fx additions
	SetFXCreationAllowed( false );

	// Invoke post-render methods
	IGameSystem::PostRenderAllSystems();

#if DEFCFG_DEFERRED_SHADING == 1
	CPostLightingView::PopDeferredShadingFrameBuffer();

	ViewOutputDeferredShading( view );
#endif

	FinishCurrentView();

	// Set int rendering parameters back to defaults
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_ENABLE_FIXED_LIGHTING, 0 );
}

void CDeferredViewRender::ViewDrawGBuffer( const CViewSetup &view, bool &bDrew3dSkybox, SkyboxVisibility_t &nSkyboxVisible,
	bool bDrawViewModel, float flWaterHeight, bool bClipWater, bool bDrawEntities )
{
	MDLCACHE_CRITICAL_SECTION();

	view_id_t oldViewID = g_CurrentViewID;
	g_CurrentViewID = VIEW_DEFERRED_GBUFFER;

	bDrew3dSkybox = false;
	if ( !bClipWater )
	{
		CSkyboxViewDeferred *pSkyView = new CSkyboxViewDeferred( this );
		if ( ( bDrew3dSkybox = pSkyView->Setup( view, true, &nSkyboxVisible ) ) != false )
			AddViewToScene( pSkyView );

		SafeRelease( pSkyView );
	}

	// Start view
	unsigned int visFlags;
	SetupVis( view, visFlags, NULL );

	CRefPtr<CGBufferView> pGBufferView = new CGBufferView( this );
	pGBufferView->Setup( view, bDrew3dSkybox, flWaterHeight, bClipWater, bDrawEntities );
	AddViewToScene( pGBufferView );

	if ( bDrawViewModel )
		DrawViewModels( view, bDrawViewModel, true );

	g_CurrentViewID = oldViewID;
}

void CDeferredViewRender::ViewDrawComposite( const CViewSetup &view, bool &bDrew3dSkybox, SkyboxVisibility_t &nSkyboxVisible,
		int nClearFlags, view_id_t viewID, bool bDrawViewModel )
{
	DrawSkyboxComposite( view, bDrew3dSkybox );

	// this allows the refract texture to be updated once per *scene* on 360
	// (e.g. once for a monitor scene and once for the main scene)
	g_viewscene_refractUpdateFrame = gpGlobals->framecount - 1;

	m_BaseDrawFlags = 0;

	SetupCurrentView( view.origin, view.angles, viewID );

	// Invoke pre-render methods
	IGameSystem::PreRenderAllSystems();

	// Start view
	unsigned int visFlags;
	SetupVis( view, visFlags, NULL );

	if ( !bDrew3dSkybox &&
		( nSkyboxVisible == SKYBOX_NOT_VISIBLE ) && ( visFlags & IVRenderView::VIEW_SETUP_VIS_EX_RETURN_FLAGS_USES_RADIAL_VIS ) )
	{
		// This covers the case where we don't see a 3dskybox, yet radial vis is clipping
		// the far plane.  Need to clear to fog color in this case.
		nClearFlags |= VIEW_CLEAR_COLOR;
		SetClearColorToFogColor( );
	}
	else
		nClearFlags |= VIEW_CLEAR_DEPTH;

	bool drawSkybox = r_skybox.GetBool();
	if ( bDrew3dSkybox || ( nSkyboxVisible == SKYBOX_NOT_VISIBLE ) )
		drawSkybox = false;

	ParticleMgr()->IncrementFrameCode();

	DrawWorldComposite( view, nClearFlags, drawSkybox );

	#ifdef SHADEREDITOR
	VisibleFogVolumeInfo_t fogVolumeInfo;
	render->GetVisibleFogVolume(view.origin, &fogVolumeInfo);
	WaterRenderInfo_t info;
	DetermineWaterRenderInfo(fogVolumeInfo, info);
	g_ShaderEditorSystem->CustomViewRender(&g_CurrentViewID, fogVolumeInfo, info);
	#endif

	DrawViewModels( view, bDrawViewModel, false );
}

void CDeferredViewRender::ViewCombineDeferredShading( const CViewSetup &view, view_id_t viewID )
{
#if DEFCFG_DEFERRED_SHADING == 1

	DrawLightPassFullscreen( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SCREENSPACE_SHADING ),
		view.width, view.height );

	g_viewscene_refractUpdateFrame = gpGlobals->framecount - 1;

	m_BaseDrawFlags = 0;

	SetupCurrentView( view.origin, view.angles, viewID );

	IGameSystem::PreRenderAllSystems();

	ParticleMgr()->IncrementFrameCode();

	MDLCACHE_CRITICAL_SECTION();

	CRefPtr<CPostLightingView> pPostLightingView = new CPostLightingView( this );
	pPostLightingView->Setup( view );
	AddViewToScene( pPostLightingView );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->ClearBuffers( false, true );

#else

#endif
}

void CDeferredViewRender::ViewOutputDeferredShading( const CViewSetup &view )
{
#if DEFCFG_DEFERRED_SHADING
	DrawLightPassFullscreen( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SCREENSPACE_COMBINE ),
		view.width, view.height );
#endif
}

void CDeferredViewRender::DrawSkyboxComposite( const CViewSetup &view, const bool &bDrew3dSkybox )
{
	if ( !bDrew3dSkybox )
		return;

	CSkyboxViewDeferred *pSkyView = new CSkyboxViewDeferred( this );
	SkyboxVisibility_t nSkyboxVisible = SKYBOX_NOT_VISIBLE;
	if ( pSkyView->Setup( view, false, &nSkyboxVisible ) )
	{
		AddViewToScene( pSkyView );

		#ifdef SHADEREDITOR
		g_ShaderEditorSystem->UpdateSkymask(false, view.x, view.y, view.width, view.height);
		#endif
	}

	SafeRelease( pSkyView );
	Assert( nSkyboxVisible == SKYBOX_3DSKYBOX_VISIBLE );
}

void CDeferredViewRender::DrawWorldComposite( const CViewSetup &view, int nClearFlags, bool bDrawSkybox )
{
	MDLCACHE_CRITICAL_SECTION();

	VisibleFogVolumeInfo_t fogVolumeInfo;

	render->GetVisibleFogVolume( view.origin, &fogVolumeInfo );

	WaterRenderInfo_t info;
	DetermineWaterRenderInfo( fogVolumeInfo, info );

	if ( info.m_bCheapWater && !fogVolumeInfo.m_bEyeInFogVolume )
	{
		CRefPtr<CSimpleWorldViewDeferred> pNoWaterView = new CSimpleWorldViewDeferred( this );
		pNoWaterView->Setup( view, nClearFlags, bDrawSkybox, fogVolumeInfo, info );
		AddViewToScene( pNoWaterView );
		return;
	}

	// Blat out the visible fog leaf if we're not going to use it
	if ( !r_ForceWaterLeaf.GetBool() )
	{
		fogVolumeInfo.m_nVisibleFogVolumeLeaf = -1;
	}

	// We can see water of some sort
	if ( !fogVolumeInfo.m_bEyeInFogVolume )
	{
		CRefPtr<CAboveWaterViewDeferred> pAboveWaterView = new CAboveWaterViewDeferred( this );
		pAboveWaterView->Setup( view, bDrawSkybox, fogVolumeInfo, info );
		AddViewToScene( pAboveWaterView );
	}
	else
	{
		CRefPtr<CUnderWaterViewDeferred> pUnderWaterView = new CUnderWaterViewDeferred( this );
		pUnderWaterView->Setup( view, bDrawSkybox, fogVolumeInfo, info );
		AddViewToScene( pUnderWaterView );
	}
}


void CDeferredViewRender::PerformLighting( const CViewSetup &view )
{
	static ConVarRef mat_fullbright( "mat_fullbright" );
	if ( mat_fullbright.GetInt() == 1 )
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->PushRenderTargetAndViewport( GetDefRT_Lightaccum() );
		pRenderContext->ClearColor4ub( 255, 255, 255, 0 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
		return;
	}

	bool bResetLightAccum = false;
#if DEFCFG_ENABLE_RADIOSITY
	const bool bRadiosityEnabled = r_deferred_radiosity.GetBool();
#else
	const bool bRadiosityEnabled = false;
#endif

	if ( bRadiosityEnabled )
		BeginRadiosity( view );

	if ( GetGlobalLight() != NULL )
	{
		struct defData_setGlobalLightState
		{
		public:
			lightData_Global_t state;

			static void Fire( defData_setGlobalLightState d )
			{
				GetDeferredExt()->CommitLightData_Global( d.state );
			};
		};

		lightData_Global_t lightDataState = GetActiveGlobalLightState();

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

		if ( !lightDataState.bEnabled )
			bResetLightAccum = true;
	}
	else
		bResetLightAccum = true;

	CViewSetup lightingView = view;

	if ( building_cubemaps.GetBool() )
		engine->GetScreenSize( lightingView.width, lightingView.height );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( GetDefRT_Lightaccum(), 0, 0, lightingView.width, lightingView.height );

	if ( bResetLightAccum )
	{
		pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
	}
	else
		DrawLightPassFullscreen( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_GLOBAL ), lightingView.width, lightingView.height );

	pRenderContext.SafeRelease();

	GetLightingManager()->RenderLights( lightingView, this );

	if ( bRadiosityEnabled )
		EndRadiosity( view );

	pRenderContext.GetFrom( materials );
	pRenderContext->PopRenderTargetAndViewport();
}



void CDeferredViewRender::DrawViewModels( const CViewSetup &view, bool drawViewmodel, bool bGBuffer )
{
	VPROF( "CViewRender::DrawViewModel" );
	tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__ );

#ifdef PORTAL //in portal, we'd like a copy of the front buffer without the gun in it for use with the depth doubler
	g_pPortalRender->UpdateDepthDoublerTexture( view );
#endif

	bool bShouldDrawPlayerViewModel = ShouldDrawViewModel( drawViewmodel );
	bool bShouldDrawToolViewModels = ToolsEnabled();

	CMatRenderContextPtr pRenderContext( materials );

	PIXEVENT( pRenderContext, "DrawViewModels" );

	// Restore the matrices
	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
	pRenderContext->PushMatrix();

	CViewSetup viewModelSetup( view );
	viewModelSetup.zNear = view.zNearViewmodel;
	viewModelSetup.zFar = view.zFarViewmodel;
	viewModelSetup.fov = view.fovViewmodel;
	viewModelSetup.m_flAspectRatio = engine->GetScreenAspectRatio();

	ITexture *pRTColor = NULL;
	ITexture *pRTDepth = NULL;
	if( view.m_eStereoEye != STEREO_EYE_MONO )
	{
		pRTColor = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(view.m_eStereoEye-1), ISourceVirtualReality::RT_Color );
		pRTDepth = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(view.m_eStereoEye-1), ISourceVirtualReality::RT_Depth );
	}

	render->Push3DView( viewModelSetup, 0, pRTColor, GetFrustum(), pRTDepth );

	if ( bGBuffer )
	{
		const float flViewmodelScale = view.zFarViewmodel / view.zFar;
		CGBufferView::PushGBuffer( false, flViewmodelScale, false );
	}
	else
	{
		pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
			DEFERRED_RENDER_STAGE_COMPOSITION );
	}

#ifdef PORTAL //the depth range hack doesn't work well enough for the portal mod (and messing with the depth hack values makes some models draw incorrectly)
				//step up to a full depth clear if we're extremely close to a portal (in a portal environment)
	extern bool LocalPlayerIsCloseToPortal( void ); //defined in C_Portal_Player.cpp, abstracting to a single bool function to remove explicit dependence on c_portal_player.h/cpp, you can define the function as a "return true" in other build configurations at the cost of some perf
	bool bUseDepthHack = !LocalPlayerIsCloseToPortal();
	if( !bUseDepthHack )
		pRenderContext->ClearBuffers( false, true, false );
#else
	const bool bUseDepthHack = true;
#endif

	// FIXME: Add code to read the current depth range
	float depthmin = 0.0f;
	float depthmax = 1.0f;

	// HACK HACK:  Munge the depth range to prevent view model from poking into walls, etc.
	// Force clipped down range
	if( bUseDepthHack )
		pRenderContext->DepthRange( 0.0f, 0.1f );

	if ( bShouldDrawPlayerViewModel || bShouldDrawToolViewModels )
	{
		CUtlVector< IClientRenderable * > opaqueViewModelList( 32 );
		CUtlVector< IClientRenderable * > translucentViewModelList( 32 );

		ClientLeafSystem()->CollateViewModelRenderables( opaqueViewModelList, translucentViewModelList );

		if ( ToolsEnabled() && ( !bShouldDrawPlayerViewModel || !bShouldDrawToolViewModels ) )
		{
			int nOpaque = opaqueViewModelList.Count();
			for ( int i = nOpaque-1; i >= 0; --i )
			{
				IClientRenderable *pRenderable = opaqueViewModelList[ i ];
				bool bEntity = pRenderable->GetIClientUnknown()->GetBaseEntity();
				if ( ( bEntity && !bShouldDrawPlayerViewModel ) || ( !bEntity && !bShouldDrawToolViewModels ) )
				{
					opaqueViewModelList.FastRemove( i );
				}
			}

			int nTranslucent = translucentViewModelList.Count();
			for ( int i = nTranslucent-1; i >= 0; --i )
			{
				IClientRenderable *pRenderable = translucentViewModelList[ i ];
				bool bEntity = pRenderable->GetIClientUnknown()->GetBaseEntity();
				if ( ( bEntity && !bShouldDrawPlayerViewModel ) || ( !bEntity && !bShouldDrawToolViewModels ) )
				{
					translucentViewModelList.FastRemove( i );
				}
			}
		}

		if ( !UpdateRefractIfNeededByList( opaqueViewModelList ) && !bGBuffer )
		{
			UpdateRefractIfNeededByList( translucentViewModelList );
		}

		DrawRenderablesInList( opaqueViewModelList );
		if (!bGBuffer)
			DrawRenderablesInList( translucentViewModelList, STUDIO_TRANSPARENCY );
		else
			pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
			                                          DEFERRED_RENDER_STAGE_INVALID );
	}

	// Reset the depth range to the original values
	if( bUseDepthHack )
		pRenderContext->DepthRange( depthmin, depthmax );

	if ( bGBuffer )
	{
		CGBufferView::PopGBuffer();
	}

	render->PopView( GetFrustum() );

	// Restore the matrices
	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
	pRenderContext->PopMatrix();
}


void CDeferredViewRender::RenderView( const CViewSetup &view, int nClearFlags, int whatToDraw )
{
	g_nShadowViewsSkippedLastFrame = g_nShadowViewsSkippedThisFrame;
	g_nShadowViewsRenderedLastFrame = g_nShadowViewsRenderedThisFrame;
	g_nShadowViewsSkippedThisFrame = 0;
	g_nShadowViewsRenderedThisFrame = 0;

	m_UnderWaterOverlayMaterial.Shutdown();					// underwater view will set

	CViewSetup worldView = view;

	CLightingEditor *pLightEditor = GetLightingEditor();

	if ( pLightEditor->IsEditorActive() && !building_cubemaps.GetBool() )
		pLightEditor->GetEditorView( &worldView.origin, &worldView.angles );
	else
		pLightEditor->SetEditorView( &worldView.origin, &worldView.angles );

	m_CurrentView = worldView;

	C_BaseAnimating::AutoAllowBoneAccess boneaccess( true, true );
	VPROF( "CViewRender::RenderView" );
	tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__ );

	CMatRenderContextPtr pRenderContext( materials );
	ITexture *saveRenderTarget = pRenderContext->GetRenderTarget();
	pRenderContext.SafeRelease(); // don't want to hold for long periods in case in a locking active share thread mode

	g_pClientShadowMgr->AdvanceFrame();

	// Must be first
	render->SceneBegin();

	pRenderContext.GetFrom( materials );
	pRenderContext->TurnOnToneMapping();
	pRenderContext.SafeRelease();

	// clear happens here probably
	SetupMain3DView( worldView, nClearFlags );

	unsigned int visFlags;
	SetupVis( worldView, visFlags, NULL );

	ProcessDeferredGlobals( worldView );
	GetLightingManager()->LightSetup( worldView );

	// Force it to clear the framebuffer if they're in solid space.
	if ( ( nClearFlags & VIEW_CLEAR_COLOR ) == 0 )
	{
		if ( enginetrace->GetPointContents( worldView.origin ) == CONTENTS_SOLID )
		{
			nClearFlags |= VIEW_CLEAR_COLOR;
		}
	}

	// Render world and all entities, particles, etc.
	ViewDrawSceneDeferred( worldView, nClearFlags, VIEW_MAIN, whatToDraw & RENDERVIEW_DRAWVIEWMODEL );

	// We can still use the 'current view' stuff set up in ViewDrawScene
	AllowCurrentViewAccess( true );

	// must happen before teardown
	pLightEditor->OnRender();

	GetLightingManager()->LightTearDown();

	engine->DrawPortals();

	DisableFog();

	// Finish scene
	render->SceneEnd();

	// Draw lightsources if enabled
	//render->DrawLights();

	RenderPlayerSprites();

	// Image-space motion blur
	if ( !building_cubemaps.GetBool() && worldView.m_bDoBloomAndToneMapping ) // We probably should use a different view. variable here
	{
		static ConVarRef mat_motion_blur_enabled( "mat_motion_blur_enabled" );
		if ( ( mat_motion_blur_enabled.GetInt() ) && ( g_pMaterialSystemHardwareConfig->GetDXSupportLevel() >= 90 ) )
		{
			pRenderContext.GetFrom( materials );
			{
				PIXEVENT( pRenderContext, "DoImageSpaceMotionBlur" );
				DoImageSpaceMotionBlur( worldView, worldView.x, worldView.y, worldView.width, worldView.height );
			}
			pRenderContext.SafeRelease();
		}
	}

	GetClientModeNormal()->DoPostScreenSpaceEffects( &worldView );

	DrawUnderwaterOverlay();

	PixelVisibility_EndScene();

	// Draw fade over entire screen if needed
	byte color[4];
	bool blend;
	vieweffects->GetFadeParams( &color[0], &color[1], &color[2], &color[3], &blend );

	// Draw an overlay to make it even harder to see inside smoke particle systems.
	DrawSmokeFogOverlay();

	// Overlay screen fade on entire screen
	IMaterial* pMaterial = blend ? m_ModulateSingleColor : m_TranslucentSingleColor;
	render->ViewDrawFade( color, pMaterial );
	PerformScreenOverlay( worldView.x, worldView.y, worldView.width, worldView.height );

	// Prevent sound stutter if going slow
	engine->Sound_ExtraUpdate();

	if ( !building_cubemaps.GetBool() && worldView.m_bDoBloomAndToneMapping )
	{
		pRenderContext.GetFrom( materials );
		{
			PIXEVENT( pRenderContext, "DoEnginePostProcessing" );

			bool bFlashlightIsOn = false;
			C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
			if ( pLocal )
			{
				bFlashlightIsOn = pLocal->IsEffectActive( EF_DIMLIGHT );
			}
			DoEnginePostProcessing( worldView.x, worldView.y, worldView.width, worldView.height, bFlashlightIsOn );
		}
		pRenderContext.SafeRelease();
	}

	#ifdef SHADEREDITOR
	g_ShaderEditorSystem->CustomPostRender();
	#endif

	// And here are the screen-space effects

	if ( IsPC() )
	{
		tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "GrabPreColorCorrectedFrame" );

		// Grab the pre-color corrected frame for editing purposes
		engine->GrabPreColorCorrectedFrame( worldView.x, worldView.y, worldView.width, worldView.height );
	}

	PerformScreenSpaceEffects( 0, 0, worldView.width, worldView.height );

	if ( g_pMaterialSystemHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER )
	{
		pRenderContext.GetFrom( materials );
		pRenderContext->SetToneMappingScaleLinear(Vector(1,1,1));
		pRenderContext.SafeRelease();
	}

	CleanupMain3DView( worldView );

	pRenderContext = materials->GetRenderContext();
	pRenderContext->SetRenderTarget( saveRenderTarget );
	pRenderContext.SafeRelease();

	// Draw the overlay
	if ( m_bDrawOverlay )
	{
		tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "DrawOverlay" );

		// This allows us to be ok if there are nested overlay views
		const CViewSetup currentView = m_CurrentView;
		CViewSetup tempView = m_OverlayViewSetup;
		tempView.fov = ScaleFOVByWidthRatio( tempView.fov, tempView.m_flAspectRatio / ( 4.0f / 3.0f ) );
		tempView.m_bDoBloomAndToneMapping = false;	// FIXME: Hack to get Mark up and running
		m_bDrawOverlay = false;
		RenderView( tempView, m_OverlayClearFlags, m_OverlayDrawFlags );
		m_CurrentView = currentView;
	}

	if ( mat_viewportupscale.GetBool() && mat_viewportscale.GetFloat() < 1.0f )
	{
		#pragma warning(suppress: 4456)
		CMatRenderContextPtr pRenderContext( materials );

		ITexture	*pFullFrameFB1 = materials->FindTexture( "_rt_FullFrameFB1", TEXTURE_GROUP_RENDER_TARGET );
		IMaterial	*pCopyMaterial = materials->FindMaterial( "dev/upscale", TEXTURE_GROUP_OTHER );
		pCopyMaterial->IncrementReferenceCount();

		Rect_t	DownscaleRect, UpscaleRect;

		DownscaleRect.x = worldView.x;
		DownscaleRect.y = worldView.y;
		DownscaleRect.width = worldView.width;
		DownscaleRect.height = worldView.height;

		UpscaleRect.x = worldView.m_nUnscaledX;
		UpscaleRect.y = worldView.m_nUnscaledY;
		UpscaleRect.width = worldView.m_nUnscaledWidth;
		UpscaleRect.height = worldView.m_nUnscaledHeight;

		pRenderContext->CopyRenderTargetToTextureEx( pFullFrameFB1, 0, &DownscaleRect, &DownscaleRect );
		pRenderContext->DrawScreenSpaceRectangle( pCopyMaterial, UpscaleRect.x, UpscaleRect.y, UpscaleRect.width, UpscaleRect.height,
			DownscaleRect.x, DownscaleRect.y, DownscaleRect.x+DownscaleRect.width-1, DownscaleRect.y+DownscaleRect.height-1,
			pFullFrameFB1->GetActualWidth(), pFullFrameFB1->GetActualHeight() );

		pCopyMaterial->DecrementReferenceCount();
	}

	// if we're in VR mode we might need to override the render target
	if( UseVR() )
	{
		saveRenderTarget = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(worldView.m_eStereoEye - 1), ISourceVirtualReality::RT_Color );
	}

	// Draw the 2D graphics
	render->Push2DView( worldView, 0, saveRenderTarget, GetFrustum() );

	Render2DEffectsPreHUD( worldView );

	if ( whatToDraw & RENDERVIEW_DRAWHUD )
	{
		VPROF_BUDGET( "VGui_DrawHud", VPROF_BUDGETGROUP_OTHER_VGUI );
		int viewWidth = worldView.m_nUnscaledWidth;
		int viewHeight = worldView.m_nUnscaledHeight;
		int viewActualWidth = worldView.m_nUnscaledWidth;
		int viewActualHeight = worldView.m_nUnscaledHeight;
		int viewX = worldView.m_nUnscaledX;
		int viewY = worldView.m_nUnscaledY;
		int viewFramebufferX = 0;
		int viewFramebufferY = 0;
		int viewFramebufferWidth = viewWidth;
		int viewFramebufferHeight = viewHeight;
		bool bClear = false;
		bool bPaintMainMenu = false;
		ITexture *pTexture = NULL;
		if( UseVR() )
		{
			if( g_ClientVirtualReality.ShouldRenderHUDInWorld() )
			{
				pTexture = materials->FindTexture( "_rt_gui", NULL, false );
				if( pTexture )
				{
					bPaintMainMenu = true;
					bClear = true;
					viewX = 0;
					viewY = 0;
					viewActualWidth = pTexture->GetActualWidth();
					viewActualHeight = pTexture->GetActualHeight();

					vgui::surface()->GetScreenSize( viewWidth, viewHeight );

					viewFramebufferX = 0;
					if( worldView.m_eStereoEye == STEREO_EYE_RIGHT && !saveRenderTarget )
						viewFramebufferX = viewFramebufferWidth;
					viewFramebufferY = 0;
				}
			}
			else
			{
				viewFramebufferX = worldView.m_eStereoEye == STEREO_EYE_RIGHT ? viewWidth : 0;
				viewFramebufferY = 0;
			}
		}

		// Get the render context out of materials to avoid some debug stuff.
		// WARNING THIS REQUIRES THE .SafeRelease below or it'll never release the ref
		pRenderContext = materials->GetRenderContext();

		// clear depth in the backbuffer before we push the render target
		if( bClear )
		{
			pRenderContext->ClearBuffers( false, true, true );
		}

		// constrain where VGUI can render to the view
		pRenderContext->PushRenderTargetAndViewport( pTexture, NULL, viewX, viewY, viewActualWidth, viewActualHeight );
		// If drawing off-screen, force alpha for that pass
		if (pTexture)
		{
			pRenderContext->OverrideAlphaWriteEnable( true, true );
		}

		// let vgui know where to render stuff for the forced-to-framebuffer panels
		if( UseVR() )
		{
			g_pMatSystemSurface->SetFullscreenViewportAndRenderTarget( viewFramebufferX, viewFramebufferY, viewFramebufferWidth, viewFramebufferHeight, saveRenderTarget );
		}

		// clear the render target if we need to
		if( bClear )
		{
			pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
		}
		pRenderContext.SafeRelease();

		tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "VGui_DrawHud", __FUNCTION__ );

		// paint the vgui screen
		VGui_PreRender();

		// Make sure the client .dll root panel is at the proper point before doing the "SolveTraverse" calls
		vgui::VPANEL root = enginevgui->GetPanel( PANEL_CLIENTDLL );
		if ( root != 0 )
		{
			vgui::ipanel()->SetSize( root, viewWidth, viewHeight );
		}
		// Same for client .dll tools
		root = enginevgui->GetPanel( PANEL_CLIENTDLL_TOOLS );
		if ( root != 0 )
		{
			vgui::ipanel()->SetSize( root, viewWidth, viewHeight );
		}

		// The crosshair, etc. needs to get at the current setup stuff
		AllowCurrentViewAccess( true );

		// Draw the in-game stuff based on the actual viewport being used
		render->VGui_Paint( PAINT_INGAMEPANELS );

		// maybe paint the main menu and cursor too if we're in stereo hud mode
		if( bPaintMainMenu )
			render->VGui_Paint( PAINT_UIPANELS | PAINT_CURSOR );

		AllowCurrentViewAccess( false );

		VGui_PostRender();

		g_pClientMode->PostRenderVGui();
		pRenderContext = materials->GetRenderContext();
		if (pTexture)
		{
			pRenderContext->OverrideAlphaWriteEnable( false, true );
		}
		pRenderContext->PopRenderTargetAndViewport();

		if ( UseVR() )
		{
			// figure out if we really want to draw the HUD based on freeze cam
			C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
			bool bInFreezeCam = ( pPlayer && pPlayer->GetObserverMode() == OBS_MODE_FREEZECAM );

			// draw the HUD after the view model so its "I'm closer" depth queues work right.
			if( !bInFreezeCam && g_ClientVirtualReality.ShouldRenderHUDInWorld() )
			{
				// Now we've rendered the HUD to its texture, actually get it on the screen.
				// Since we're drawing it as a 3D object, we need correctly set up frustum, etc.
				int ClearFlags = 0;
				SetupMain3DView( worldView, ClearFlags );

				// TODO - a bit of a shonky test - basically trying to catch the main menu, the briefing screen, the loadout screen, etc.
				bool bTranslucent = !g_pMatSystemSurface->IsCursorVisible();
				g_ClientVirtualReality.RenderHUDQuad( g_pClientMode->ShouldBlackoutAroundHUD(), bTranslucent );
				CleanupMain3DView( worldView );
			}
		}

		pRenderContext->Flush();
		pRenderContext.SafeRelease();
	}

	CDebugViewRender::Draw2DDebuggingInfo( worldView );

	Render2DEffectsPostHUD( worldView );

	// We can no longer use the 'current view' stuff set up in ViewDrawScene
	AllowCurrentViewAccess( false );

	if ( IsPC() )
	{
		CDebugViewRender::GenerateOverdrawForTesting();
	}

	render->PopView( GetFrustum() );
	FlushWorldLists();

	m_CurrentView = worldView;
}

void CDeferredViewRender::ProcessDeferredGlobals( const CViewSetup &view )
{
	VMatrix matPerspective, matView, matViewProj, screen2world;
	matView.Identity();
	matView.SetupMatrixOrgAngles( vec3_origin, view.angles );

	MatrixSourceToDeviceSpace( matView );

	#ifdef SHADEREDITOR
	g_ShaderEditorSystem->SetMainViewMatrix( matView );
	#endif

	matView = matView.Transpose3x3();
	Vector viewPosition;

	Vector3DMultiply( matView, view.origin, viewPosition );
	matView.SetTranslation( -viewPosition );
	MatrixBuildPerspectiveX( matPerspective, view.fov, view.m_flAspectRatio,
		view.zNear, view.zFar );
	MatrixMultiply( matPerspective, matView, matViewProj );

	MatrixInverseGeneral( matViewProj, screen2world );

	GetLightingManager()->SetRenderConstants( screen2world, view );

	Vector frustum_c0, frustum_cc, frustum_1c;
	float projDistance = 1.0f;
	Vector3DMultiplyPositionProjective( screen2world, Vector(0,projDistance,projDistance), frustum_c0 );
	Vector3DMultiplyPositionProjective( screen2world, Vector(0,0,projDistance), frustum_cc );
	Vector3DMultiplyPositionProjective( screen2world, Vector(projDistance,0,projDistance), frustum_1c );

	frustum_c0 -= view.origin;
	frustum_cc -= view.origin;
	frustum_1c -= view.origin;

	Vector frustum_up = frustum_c0 - frustum_cc;
	Vector frustum_right = frustum_1c - frustum_cc;

	frustum_cc /= view.zFar;
	frustum_right /= view.zFar;
	frustum_up /= view.zFar;

	Vector fwd;
	AngleVectors( view.angles, &fwd );

	VMatrix frustumDeltas;
	frustumDeltas.Identity();
	frustumDeltas.SetBasisVectors( frustum_cc, frustum_right, frustum_up );

#if DEFCFG_BILATERAL_DEPTH_TEST
	VMatrix matWorldToCameraDepthTex;
	MatrixBuildScale( matWorldToCameraDepthTex, 0.5f, -0.5f, 1.0f );
	matWorldToCameraDepthTex[0][3] = matWorldToCameraDepthTex[1][3] = 0.5f;
	MatrixMultiply( matWorldToCameraDepthTex, matViewProj, matWorldToCameraDepthTex );

	QUEUE_FIRE( CommitCommonData, view.origin, fwd, view.zNear, view.zFar, frustumDeltas.Transpose3x3(), matWorldToCameraDepthTex.Transpose() );
#else
	QUEUE_FIRE( CommitCommonData, view.origin, fwd, view.zNear, view.zFar, frustumDeltas.Transpose3x3() );
#endif
}

