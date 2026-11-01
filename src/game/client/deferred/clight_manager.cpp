#include "cbase.h"
#include "c_physicsprop.h"
#include "c_gib.h"
#include "c_baseviewmodel.h"
#include "c_prop_vehicle.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/vgui/vgui_deferred.h"
#include "deferred/deferred_rt.h"
#include "deferred/cvolume_culler.h"
#include "physics_shared.h"

#include "collisionutils.h"
#include "materialsystem/imaterialvar.h"

#include "viewrender.h"
#include "view_shared.h"
#include "engine/ivdebugoverlay.h"
#include "tier0/fasttimer.h"
#include "tier1/callqueue.h"

#include "tier0/memdbgon.h"


static CLightingManager __g_lightingMan;

CLightingManager* GetLightingManager()
{
	return &__g_lightingMan;
}

static void LightingResourceRelease()
{
	GetLightingManager()->OnMaterialReload();
}

CLightingManager::CLightingManager() : BaseClass( "LightingManagerSystem" )
{
#if DEBUG
	m_bVolatileLists = false;
#endif

	m_bDrawWorldLights = true;

	MatrixSetIdentity( m_matScreenToWorld );

	m_vecViewOrigin.Init();
	m_vecForward.Init();
	m_flzNear          = 0;
	m_pCachedPropStates = NULL;
	GetLightingManager()->ResetStaticAtlasSlots();
	m_bDrawVolumetrics = false;
#if DEFCFG_USE_SSE
	m_pSortDataX4     = NULL;
	m_uiSortDataCount = 0;
#endif
}

CLightingManager::~CLightingManager() {}

bool CLightingManager::Init()
{
	materials->AddReleaseFunc( LightingResourceRelease );
	if ( cl_entitylist )
	{
		cl_entitylist->AddListenerEntity( this );
	}
	m_pCachedPropStates = new CachedPropState_t[ NUM_ENT_ENTRIES ];
	Q_memset( m_pCachedPropStates, 0, sizeof( CachedPropState_t ) * NUM_ENT_ENTRIES );
	return true;
}

void CLightingManager::Shutdown()
{
	if ( cl_entitylist )
	{
		cl_entitylist->RemoveListenerEntity( this );
	}
	if ( m_pCachedPropStates )
	{
		delete[] m_pCachedPropStates;
		m_pCachedPropStates = NULL;
	}

	for ( int i = m_hDeferredTempLights.Count() - 1; i >= 0; i-- )
	{
		def_light_temp_t* light = m_hDeferredTempLights[i];
		RemoveLight( light );
		delete light;
	}
	m_hDeferredTempLights.Purge();

	Assert( m_hDeferredLights.Count() == 0 );
	materials->RemoveReleaseFunc( LightingResourceRelease );
	ResetStaticAtlasSlots();

#if DEFCFG_USE_SSE
	if ( m_pSortDataX4 != NULL )
	{
		MemAlloc_FreeAligned( m_pSortDataX4 );
		m_pSortDataX4 = NULL;
	}
#endif
}

void CLightingManager::LevelInitPostEntity()
{
	ResetStaticAtlasSlots();
	if ( m_pCachedPropStates )
	{
		Q_memset( m_pCachedPropStates, 0, sizeof( CachedPropState_t ) * NUM_ENT_ENTRIES );
	}

	for ( int i = 0; i < m_hDeferredLights.Count(); i++ )
	{
		def_light_t *l = m_hDeferredLights[i];
		if ( !l )
			continue;

		l->m_nCachedAtlasSlot = -1;
		if ( r_deferred_shadow_cache.GetBool() && l->IsStatic() && ( l->IsSpot() || l->IsPoint() ) )
		{
			AssignStaticAtlasSlot( l );
			l->SetShadowDirty( true );
		}
	}
}

void CLightingManager::LevelShutdownPostEntity()
{
	for ( int i = m_hDeferredTempLights.Count() - 1; i >= 0; i-- )
	{
		def_light_temp_t* light = m_hDeferredTempLights[i];
		RemoveLight( light );
		delete light;
	}
	m_hDeferredTempLights.Purge();

	m_hRenderLights.Purge();

	for ( int i = 0; i < LSORT_COUNT; i++ )
		m_hPreSortedLights[i].Purge();

	for ( int i = 0; i < m_hDeferredLights.Count(); i++ )
	{
		if ( m_hDeferredLights[i] )
			m_hDeferredLights[i]->m_nCachedAtlasSlot = -1;
	}

	ResetStaticAtlasSlots();
	if ( m_pCachedPropStates )
	{
		Q_memset( m_pCachedPropStates, 0, sizeof( CachedPropState_t ) * NUM_ENT_ENTRIES );
	}
}


void CLightingManager::Update( float ft )
{
}

void CLightingManager::SetRenderWorldLights( bool bRender )
{
	m_bDrawWorldLights = bRender;
}

void CLightingManager::SetRenderConstants( const VMatrix&    ScreenToWorld,
                                           const CViewSetup& setup )
{
	m_matScreenToWorld = ScreenToWorld;

	m_vecViewOrigin = setup.origin;
	AngleVectors( setup.angles, &m_vecForward );

	m_flzNear = setup.zNear;
}

void CLightingManager::LightSetup( const CViewSetup& setup )
{
	m_vecViewOrigin = setup.origin;
	AngleVectors( setup.angles, &m_vecForward );
	m_flzNear = setup.zNear;

	// Remove lights that have run out of time
	UpdateTemplights( gpGlobals->frametime );

	PrepareLights();

	if ( r_deferred_shadow_cache.GetBool() )
	{
		CheckDynamicEntityIntersections();
	}

#if DEBUG
	m_bVolatileLists = true;
#endif

	CullLights();

#if DEFCFG_USE_SSE
	if ( m_bSortDataNeedsRealloc )
	{
		AllocateSortDataBuffer();
		m_bSortDataNeedsRealloc = false;
	}

	BuildLightSortDataBuffer();
#endif

	SortLights();

	if ( r_deferred_light_stats.GetInt() >= 2 )
	{
		DebugLights_Draw_Boundingboxes();
	}
}

void CLightingManager::LightTearDown()
{
	DoSceneDebug();

	ClearTmpLists();

#if DEBUG
	m_bVolatileLists = false;
#endif
}

void CLightingManager::OnCookieStringReceived( const char* pszString, const int& index )
{
	ITexture* pTexCookie = materials->FindTexture( pszString, TEXTURE_GROUP_OTHER );

	if ( IsErrorTexture( pTexCookie ) )
		return;
}

void CLightingManager::OnMaterialReload()
{
	FOR_EACH_VEC_FAST( def_light_t*, m_hDeferredLights, l )
	{
		l->ClearCookie();
		l->MakeDirtyAll();
		l->SetShadowDirty( true );
	}
	FOR_EACH_VEC_FAST_END
}

void CLightingManager::AddLight( def_light_t* l )
{
	Assert( !m_hDeferredLights.HasElement( l ) );

	m_hDeferredLights.AddToTail( l );

	if ( r_deferred_shadow_cache.GetBool() && l->IsStatic() && ( l->IsSpot() || l->IsPoint() ) && !l->HasValidAtlasSlot() )
	{
		AssignStaticAtlasSlot( l );
		l->SetShadowDirty( true );
	}

#if DEFCFG_USE_SSE
	m_bSortDataNeedsRealloc = true;
#endif
}

bool CLightingManager::RemoveLight( def_light_t* l )
{
	FreeStaticAtlasSlot( l );
#if DEBUG
	AssertMsg( m_bVolatileLists == false, "You MUST NOT remove lights while rendering." );
#endif

#if DEFCFG_USE_SSE
	const bool bSuccess = m_hDeferredLights.FindAndRemove( l );
	//if ( bSuccess )
	//	m_bSortDataNeedsRealloc = true;
	return bSuccess;
#else
	return m_hDeferredLights.FindAndRemove( l );
#endif
}

bool CLightingManager::IsLightRendered( def_light_t* l ) const
{
#if DEBUG
	AssertMsg( m_bVolatileLists, "Only works correctly during rendering." );
#endif

	return m_hRenderLights.HasElement( l );
}

int CLightingManager::CountTempLights() const
{
	return m_hDeferredTempLights.Count();
}

void CLightingManager::AddTempLight( def_light_temp_t* l )
{
	l->pos = SanitizeLightOrigin( l->pos );
	l->iShadow_Dist = l->iVisible_Dist;
	l->iShadow_Range = l->iVisible_Range;
	l->flStartRadius = l->flRadius;
	l->vecStartColor = l->col_diffuse;
	l->MakeDirtyAll();
	m_hDeferredTempLights.AddToTail( l );
	AddLight( l );
}

void CLightingManager::UpdateTemplights( float deltaTime )
{
	for ( int i = m_hDeferredTempLights.Count() - 1; i >= 0; i-- )
	{
		def_light_temp_t* light = m_hDeferredTempLights[i];
		if ( light->fEndLifeTime <= gpGlobals->curtime || light->fLifeDuration <= 0.0f )
		{
			RemoveLight( light );
			m_hDeferredTempLights.Remove( i );
			delete light;
		}
		else
		{
			const float flFrac = clamp( ( light->fEndLifeTime - gpGlobals->curtime ) / light->fLifeDuration, 0.0f, 1.0f );
			light->flRadius = light->flStartRadius * flFrac;
			light->col_diffuse = light->vecStartColor * flFrac;
		}
	}
}


ConVar r_deferred_light_distance_fade(
	"r_deferred_light_distance_fade",
	"0",
	FCVAR_ARCHIVE,
	"distance attenuation fading for deferred dynamic lights"
);

ConVar r_deferred_shadow_dist_min(
	"r_deferred_shadow_dist_min",
	"4096",
	FCVAR_ARCHIVE,
	"minimum distance for dynamic light shadow fade"
);

ConVar r_deferred_shadow_max(
	"r_deferred_shadow_max",
	"64",
	FCVAR_ARCHIVE,
	"maximum simultaneous dynamic shadow casters"
);

void CLightingManager::ClearTmpLists()
{
	m_hRenderLights.RemoveAll();

	for ( int i = 0; i < LSORT_COUNT; i++ )
		m_hPreSortedLights[i].RemoveAll();
}

void CLightingManager::PrepareLights()
{
	FOR_EACH_VEC_FAST( def_light_t*, m_hDeferredLights, l )
	{
		l->UpdateCookieTexture();

		if ( !l->IsDirty() )
		{
			continue;
		}

		if ( l->IsDirtyXForms() )
		{
			if ( l->IsSpot() )
				l->UpdateMatrix();

			l->UpdateXForms();
		}

		if ( l->IsDirtyRenderMesh() )
		{
			l->UpdateRenderMesh();
			l->UpdateVolumetrics();
		}

		l->UnDirtyAll();
	}
	FOR_EACH_VEC_FAST_END
}

void CLightingManager::CullLights()
{
	Assert( m_hRenderLights.Count() == 0 );

	FOR_EACH_VEC_FAST( def_light_t*, m_hDeferredLights, l )
	{
		if ( !m_bDrawWorldLights && l->bWorldLight )
			continue;

		if ( engine->CullBox( l->bounds_min_naive, l->bounds_max_naive ) )
			continue;

		Vector vecClosest;
		CalcClosestPointOnAABB( l->bounds_min_naive, l->bounds_max_naive, m_vecViewOrigin, vecClosest );
		Vector veclightDelta = vecClosest - m_vecViewOrigin;

		if ( r_deferred_light_distance_fade.GetBool() && ( veclightDelta.LengthSqr() > l->flMaxDistSqr ) )
			continue;

		const float flEffectiveShadowDist  = ( r_deferred_shadow_dist_min.GetFloat() > 0.0f )
			                                   ? r_deferred_shadow_dist_min.GetFloat()
			                                   : (float)l->iShadow_Dist;
		const float flEffectiveShadowRange = ( l->iShadow_Range > 0 ) ? (float)l->iShadow_Range : 1024.0f;

		l->flDistance_ViewOrigin = veclightDelta.Length();
		l->flShadowFade          = l->HasShadow()
			                           ? ( SATURATE( ( l->flDistance_ViewOrigin - flEffectiveShadowDist ) / flEffectiveShadowRange ) )
			                           : 1.0f;

		m_hRenderLights.AddToTail( l );
	}
	FOR_EACH_VEC_FAST_END
}

FORCEINLINE float CLightingManager::DoLightStyle( def_light_t* l )
{
	if ( !l->HasLightstyle() )
		return 1.0f;

	if ( gpGlobals->curtime > l->flLastRandomTime )
	{
		l->flLastRandomTime = gpGlobals->curtime + 1.0f - Min( 1.f, l->flStyle_Speed );

		random->SetSeed( l->iStyleSeed );
		l->flLastRandomValue = 1.0f - random->RandomFloat( 0, Min( 1.f, l->flStyle_Amount ) );
	}

	const float ran = l->flLastRandomValue;

	float sine = FastCos( gpGlobals->curtime * l->flStyle_Speed + l->iStyleSeed ) * 0.5f + 0.5f;
	sine       = Bias( sine, l->flStyle_Smooth );

	return Lerp( l->flStyle_Random, Lerp( l->flStyle_Amount, 1.0f, sine ), ran );
}

/* Which data to commit and how:

	Point lights:

	position		x y z	radius			w
	color diffuse	x y z	falloffpower	w
	color ambient	x y z

	** shadowed or cookied **
							shadowscale		w

	rotationmatrix	x y z
					x y z
					x y z

	max num constants: 10 * 3 +				-- simple
						3 * 6 +				-- cookie
						3 * 6 +				-- shadowed
						2 * 6				-- shadowed cookie


	Spot lights:

	position			x y z	radius			w
	color diffuse		x y z	falloffpower	w
	color ambient		x y z	cone outer		w
	backward			x y z	cone inner		w

	** shadowed or cookied **
	shadowscale			x

	world2texture		x y z w
						x y z w
						x y z w
						x y z w

	max num constants: 10 * 4 +				-- simple
						3 * 9 +				-- cookie
						3 * 9 +				-- shadowed
						2 * 9				-- shadowed cookie
	*/

FORCEINLINE int CLightingManager::WriteLight( def_light_t* l, float* pfl4, int bShadow, int bCookie )
{
	const bool bShadowActual = ( bShadow >= 0 ) ? ( bShadow != 0 ) : l->ShouldRenderShadow();
	const bool bCookieActual = ( bCookie >= 0 ) ? ( bCookie != 0 ) : ( l->HasCookie() && l->IsCookieReady() );
	const bool bAdvanced = bShadowActual || bCookieActual || l->HasUberlight();

	int numConsts = 0;

	const float flLightstyle = DoLightStyle( l );
	const float flDistance   = l->flDistance_ViewOrigin;
	const float flMasterFade = r_deferred_light_distance_fade.GetBool()
		                           ? ( flLightstyle * ( 1.0f - SATURATE( ( flDistance - l->iVisible_Dist ) / l->iVisible_Range ) ) )
		                           : flLightstyle;

	const int iFloatSize = sizeof( float );

	const Vector& colDiffuse = l->col_diffuse * flMasterFade;
	const Vector& colAmbient = l->col_ambient * flMasterFade;

	switch ( l->iLighttype )
	{
	default:
		Assert(0);
	case DEFLIGHTTYPE_POINT:
		{
			numConsts = bAdvanced ? NUM_CONSTS_POINT_ADVANCED : NUM_CONSTS_POINT_SIMPLE;

			Q_memcpy( pfl4, l->pos.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flRadius;
			pfl4++;

			Q_memcpy( pfl4, colDiffuse.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flFalloffPower;
			pfl4++;

			Q_memcpy( pfl4, colAmbient.Base(), iFloatSize * 3 );
			pfl4 += 3;

			if ( bAdvanced )
			{
				*pfl4 = l->flShadowFade;
				pfl4++;

				VMatrix rotMatrix, rotMatrixit;
				rotMatrix.Identity();
				rotMatrix.SetupMatrixOrgAngles( vec3_origin, l->ang );
				MatrixSourceToDeviceSpaceInv( rotMatrix );
				rotMatrix = rotMatrix.Transpose3x3();
				MatrixInverseGeneral( rotMatrix, rotMatrixit );
				Q_memcpy( pfl4, rotMatrixit.Base(), iFloatSize * 12 );
			}
		}
		break;
	case DEFLIGHTTYPE_SPOT:
		{
			numConsts = bAdvanced ? NUM_CONSTS_SPOT_ADVANCED : NUM_CONSTS_SPOT_SIMPLE;

			Q_memcpy( pfl4, l->pos.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flRadius;
			pfl4++;

			Q_memcpy( pfl4, colDiffuse.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flFalloffPower;
			pfl4++;

			Q_memcpy( pfl4, colAmbient.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flSpotCone_Outer;
			pfl4++;

			Q_memcpy( pfl4, l->backDir.Base(), iFloatSize * 3 );
			pfl4 += 3;

			*pfl4 = l->flSpotCone_Inner;
			pfl4++;

			if ( bAdvanced )
			{
				pfl4[0] = l->flShadowFade;
				pfl4[1] = l->HasUberlight() ? 1.0f : 0.0f;
				pfl4[2] = l->flUberRoundness;
				pfl4[3] = 0.0f;
				pfl4 += 4;

				Q_memcpy( pfl4, l->spotWorldToTex.Base(), iFloatSize * 16 );
				pfl4 += 16;

				pfl4[0] = l->flUberWidth;
				pfl4[1] = l->flUberHeight;
				pfl4[2] = l->flUberWEdge;
				pfl4[3] = l->flUberHEdge;
				pfl4 += 4;

				pfl4[0] = l->flUberShearX;
				pfl4[1] = l->flUberShearY;
				pfl4[2] = l->flUberCutOn;
				pfl4[3] = l->flUberCutOff;
				pfl4 += 4;
			}
		}
		break;
	}

	return numConsts;
}

FORCEINLINE bool SortLightsByComboType( const CUtlVector<def_light_t*>& hLights,
                                        CUtlVector<def_light_t*>& hLightsShadowedCookie,
                                        CUtlVector<def_light_t*>& hLightsShadowed,
                                        CUtlVector<def_light_t*>& hLightsCookied,
                                        CUtlVector<def_light_t*>& hLightsSimple,
                                        int nMaxShadows )
{
	hLightsShadowedCookie.RemoveAll();
	hLightsShadowed.RemoveAll();
	hLightsCookied.RemoveAll();
	hLightsSimple.RemoveAll();

	int nShadowsAssigned = 0;

	FOR_EACH_VEC_FAST( def_light_t* const, hLights, l )
	{
		const bool bWantsShadow = l->ShouldRenderShadow();
		const bool bShadowed    = bWantsShadow && ( nShadowsAssigned < nMaxShadows );
		const bool bCookie      = l->HasCookie() && l->IsCookieReady();
		const bool bUber        = l->HasUberlight();

		if ( bShadowed )
		{
			nShadowsAssigned++;
			l->bWasShadowActive = true;
		}
		else
		{
			l->bWasShadowActive = false;
		}

		if ( bShadowed && bCookie )
			hLightsShadowedCookie.AddToTail( l );
		else if ( bShadowed )
			hLightsShadowed.AddToTail( l );
		else if ( bCookie || bUber )
			hLightsCookied.AddToTail( l );
		else
			hLightsSimple.AddToTail( l );
	}
	FOR_EACH_VEC_FAST_END

	Assert( hLights.Count() ==
		hLightsShadowedCookie.Count() + hLightsShadowed.Count() +
		hLightsCookied.Count() + hLightsSimple.Count() );

	return hLights.Count() > 0;
}

FORCEINLINE void CLightingManager::DrawVolumePrepass( bool bDoModelTransform, const CViewSetup& view, def_light_t* l )
{
	if ( !l->IsSpot() )
		return;

	Assert( l->pMesh_VolumPrepass != NULL );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( GetDefRT_VolumePrepass(), 0, 0, view.width / 4, view.height / 4 );
	pRenderContext->ClearColor3ub( 0, 0, 0 );
	pRenderContext->ClearBuffers( true, false );

	if ( bDoModelTransform )
	{
		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->PushMatrix();
		pRenderContext->LoadMatrix( l->worldTransform );
	}

	pRenderContext->Bind( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_PREPASS ) );
	l->pMesh_VolumPrepass->Draw();

	if ( bDoModelTransform )
	{
		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->PopMatrix();
	}

	pRenderContext->PopRenderTargetAndViewport();
}

void CLightingManager::RenderLights( const CViewSetup& view, CDeferredViewRender* pCaller )
{
	static CUtlVector<def_light_t*> lightsShadowedCookied;
	static CUtlVector<def_light_t*> lightsShadowed;
	static CUtlVector<def_light_t*> lightsCookied;
	static CUtlVector<def_light_t*> lightsSimple;

#if DEFCFG_EXTRA_SORT
	struct Lightpass_t
	{
		CUtlVector<def_light_t*> *lightVecFullscreenSimple;
		CUtlVector<def_light_t*> *lightVecFullscreenAdvanced;
		CUtlVector<def_light_t*> *lightVecWorldSimple;
		CUtlVector<def_light_t*> *lightVecWorldAdvanced;
		IMaterial *pMatPassFullscreen;
		IMaterial *pMatPassWorld;
		IMaterial *pMatVolumeFullscreen;
		IMaterial *pMatVolumeWorld;
		int constCount_simple;
		int constCount_advanced;
	};

	Lightpass_t lightTypes[] =
	{
		{ &m_hPreSortedLights[ LSORT_POINT_FULLSCREEN_SIMPLE ], &m_hPreSortedLights[ LSORT_POINT_FULLSCREEN_ADVANCED ],
		 &m_hPreSortedLights[ LSORT_POINT_WORLD_SIMPLE ], &m_hPreSortedLights[ LSORT_POINT_WORLD_ADVANCED ],//JACK: Here we could be sorting into
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_POINT_FULLSCREEN ),						//		simple and advanced lights as
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_POINT_WORLD ),								//		simple lights only need  a single
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_POINT_FULLSCREEN ),					//		state change (model proj) to render
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_POINT_WORLD ),
		NUM_CONSTS_POINT_SIMPLE, NUM_CONSTS_POINT_ADVANCED },

		{ &m_hPreSortedLights[ LSORT_SPOT_FULLSCREEN_SIMPLE ], &m_hPreSortedLights[ LSORT_SPOT_FULLSCREEN_ADVANCED ],
		&m_hPreSortedLights[ LSORT_SPOT_WORLD_SIMPLE ], &m_hPreSortedLights[ LSORT_SPOT_WORLD_ADVANCED ],
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_SPOT_FULLSCREEN ),
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_SPOT_WORLD ),
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_SPOT_FULLSCREEN ),
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_SPOT_WORLD ),
		NUM_CONSTS_SPOT_SIMPLE, NUM_CONSTS_SPOT_ADVANCED },
	};
#else
	struct Lightpass_t
	{
		CUtlVector<def_light_t*>* lightVecFullscreen;
		CUtlVector<def_light_t*>* lightVecWorld;
		IMaterial*                pMatPassFullscreen;
		IMaterial*                pMatPassWorld;
		IMaterial*                pMatVolumeFullscreen;
		IMaterial*                pMatVolumeWorld;
		int                       constCount_simple;
		int                       constCount_advanced;
	};

	Lightpass_t lightTypes[] =
	{
		{
			&m_hPreSortedLights[LSORT_POINT_FULLSCREEN], &m_hPreSortedLights[LSORT_POINT_WORLD],
			//JACK: Here we could be sorting into
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_POINT_FULLSCREEN ), //		simple and advanced lights as
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_POINT_WORLD ),      //		simple lights only need  a single
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_POINT_FULLSCREEN ),
			//		state change (model proj) to render
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_POINT_WORLD ),
			NUM_CONSTS_POINT_SIMPLE, NUM_CONSTS_POINT_ADVANCED
		},

		{
			&m_hPreSortedLights[LSORT_SPOT_FULLSCREEN], &m_hPreSortedLights[LSORT_SPOT_WORLD],
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_SPOT_FULLSCREEN ),
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_SPOT_WORLD ),
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_SPOT_FULLSCREEN ),
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_SPOT_WORLD ),
			NUM_CONSTS_SPOT_SIMPLE, NUM_CONSTS_SPOT_ADVANCED
		},
	};
#endif
	const int iNumLightTypes = ARRAYSIZE( lightTypes );

	int iPassesDrawnFullscreen[iNumLightTypes] = { 0 };
	int iPassesVolumetrics[2]                  = { 0 };
	int iDrawnShadowed                         = 0;
	int iDrawnCookied                          = 0;
	int iDrawnSimple                           = 0;

	ITexture* pVolumBuffer0 = GetDefRT_VolumetricsBuffer( 0 );

	if ( m_bDrawVolumetrics )
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->PushRenderTargetAndViewport( pVolumBuffer0, 0, 0, view.width / 4, view.height / 4 );
		pRenderContext->ClearColor3ub( 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
	}

	struct queueVolume
	{
		queueVolume( def_light_t* l, int offset_data, int offset_sampler )
		{
			dataoffset    = offset_data;
			sampleroffset = offset_sampler;
			pLight        = l;
		};

		int          dataoffset;
		int          sampleroffset;
		def_light_t* pLight;
	};

	const int nMaxShadows = r_deferred_shadow_max.GetInt();
	for ( int i = 0; i < iNumLightTypes; i++ )
	{
		if ( SortLightsByComboType( *( lightTypes[i].lightVecFullscreen ),
		                            lightsShadowedCookied, lightsShadowed, lightsCookied, lightsSimple, nMaxShadows ) )
		{
			while ( lightsShadowedCookied.Count() > 0 || lightsShadowed.Count() > 0 ||
				lightsCookied.Count() > 0 || lightsSimple.Count() > 0 )
			{
				int drawShadowedCookied = Min( MAX_LIGHTS_SHADOWEDCOOKIE, lightsShadowedCookied.Count() );
				int drawShadowed        = Min( MAX_LIGHTS_SHADOWED, lightsShadowed.Count() );
				int drawCookied         = Min( MAX_LIGHTS_COOKIE, lightsCookied.Count() );
				int drawSimple          = Min( MAX_LIGHTS_SIMPLE, lightsSimple.Count() );

				int numRows = ( drawShadowedCookied * lightTypes[i].constCount_advanced +
					drawShadowed * lightTypes[i].constCount_advanced +
					drawCookied * lightTypes[i].constCount_advanced +
					drawSimple * lightTypes[i].constCount_simple );

				int memToAlloc = 4 * numRows;

				Assert( memToAlloc > 0 );

				lightDataCommon_t* pFlLightDataBlock = new lightDataCommon_t( memToAlloc );

				Assert( pFlLightDataBlock != NULL && pFlLightDataBlock->pFlData != NULL );

				if ( pFlLightDataBlock != NULL )
				{
					static CUtlVector<def_light_t*> commitableLights;
					static CUtlVector<queueVolume>  volumeLights;

					commitableLights.AddMultipleToTail( drawShadowedCookied, lightsShadowedCookied.Base() );
					commitableLights.AddMultipleToTail( drawShadowed, lightsShadowed.Base() );
					commitableLights.AddMultipleToTail( drawCookied, lightsCookied.Base() );
					commitableLights.AddMultipleToTail( drawSimple, lightsSimple.Base() );

					float* pFlWriter = pFlLightDataBlock->pFlData;

					for ( int k = 0; k < drawShadowedCookied; k++ )
						pFlWriter += WriteLight( lightsShadowedCookied[k], pFlWriter, 1, 1 ) * 4;

					for ( int k = 0; k < drawShadowed; k++ )
						pFlWriter += WriteLight( lightsShadowed[k], pFlWriter, 1, 0 ) * 4;

					for ( int k = 0; k < drawCookied; k++ )
						pFlWriter += WriteLight( lightsCookied[k], pFlWriter, 0, 1 ) * 4;

					for ( int k = 0; k < drawSimple; k++ )
						pFlWriter += WriteLight( lightsSimple[k], pFlWriter, 0, 0 ) * 4;

					Assert( pFlWriter - pFlLightDataBlock->pFlData == memToAlloc );

					iPassesDrawnFullscreen[i]++;

					//JACK: Here we can use the MAX_LIGHTS_X macros to mean the max light textures to use
					//		and render multiple lower LOD lights to the same texture, will need to commit the
					//		number of lights to draw, light sample lod, and texture id,

					for ( int iShadowed = 0; iShadowed < ( drawShadowedCookied + drawShadowed ); iShadowed++ )
					{
						def_light_t* pLight = commitableLights[iShadowed];
						pCaller->DrawLightShadowView( view, iShadowed, pLight );

						if ( pLight->IsSpot() )
						{
							if ( pLight->IsStatic() && pLight->HasValidAtlasSlot() )
							{
								QUEUE_FIRE( CommitTexture_ProjectedDepth, iShadowed, GetShadowColorRT_Static( pLight->m_nCachedAtlasSlot ) );
							}
							else
							{
								QUEUE_FIRE( CommitTexture_ProjectedDepth, iShadowed, GetShadowColorRT_Proj( iShadowed ) );
							}
						}
						else if ( pLight->IsPoint() )
						{
							if ( pLight->IsStatic() && pLight->HasValidAtlasSlot() )
							{
								QUEUE_FIRE( CommitTexture_DualParaboloidDepth, iShadowed, GetShadowColorRT_StaticPoint( pLight->m_nCachedAtlasSlot ) );
							}
							else
							{
								QUEUE_FIRE( CommitTexture_DualParaboloidDepth, iShadowed, GetShadowColorRT_DP( iShadowed ) );
							}
						}

						if ( pLight->HasVolumetrics() && r_deferred_volumetric_intensity.GetFloat() > 0.0f )
						{
							const int iDataOffset	 = iShadowed * lightTypes[i].constCount_advanced * 4;
							const int iSamplerOffset = iShadowed;

							volumeLights.AddToTail( queueVolume( pLight, iDataOffset, iSamplerOffset ) );
						}
					}

					commitableLights.RemoveAll();

					pFlLightDataBlock->numRows            = numRows;
					pFlLightDataBlock->numShadowedCookied = drawShadowedCookied;
					pFlLightDataBlock->numShadowed        = drawShadowed;
					pFlLightDataBlock->numCookied         = drawCookied;
					pFlLightDataBlock->numSimple          = drawSimple;
					QUEUE_FIRE( CommitLightData_Common, pFlLightDataBlock );

					iDrawnShadowed += drawShadowedCookied + drawShadowed;
					iDrawnCookied += drawShadowedCookied + drawCookied;
					iDrawnSimple += drawSimple;

					static CUtlVector<def_light_t*> cookiedLights;
					cookiedLights.AddMultipleToTail( drawShadowedCookied, lightsShadowedCookied.Base() );
					cookiedLights.AddMultipleToTail( drawCookied, lightsCookied.Base() );

					for ( int iCookied = 0; iCookied < ( drawShadowedCookied + drawCookied ); iCookied++ )
					{
						ITexture* pCookie = cookiedLights[iCookied]->GetCookieForDraw( iCookied );

						Assert( pCookie != NULL );

						QUEUE_FIRE( CommitTexture_Cookie, iCookied, pCookie );
					}

					cookiedLights.RemoveAll();

					DrawLightPassFullscreen( lightTypes[i].pMatPassFullscreen, view.width, view.height );

					FOR_EACH_VEC_FAST( queueVolume, volumeLights, entry )
					{
#if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
						const float flCameraLightDelta = (view.origin).DistTo(entry.pLight->pos);
						int iVolumeLOD = 4;
						if( flCameraLightDelta < entry.pLight->flVolumeLOD0Dist )
						{
							iVolumeLOD = 0;
						}
						else if( flCameraLightDelta < entry.pLight->flVolumeLOD1Dist )
						{
							iVolumeLOD = 1;
						}
						else if( flCameraLightDelta < entry.pLight->flVolumeLOD2Dist )
						{
							iVolumeLOD = 2;
						}
						else if( flCameraLightDelta < entry.pLight->flVolumeLOD3Dist )
						{
							iVolumeLOD = 3;
						}
#endif
						volumeData_t data;
						data.iDataOffset    = entry.dataoffset;
						data.iSamplerOffset = entry.sampleroffset;
						data.iNumRows       = lightTypes[i].constCount_advanced;
						data.bHasCookie     = entry.pLight->HasCookie();
#if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
					data.iLOD = iVolumeLOD;
#endif
#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
						data.iSamples = entry.pLight->iVolumeSamples;
#endif
						QUEUE_FIRE( CommitVolumeData, data );

						DrawVolumePrepass( true, view, entry.pLight );

						CMatRenderContextPtr pRenderContext( materials );
						pRenderContext->PushRenderTargetAndViewport( pVolumBuffer0, 0, 0, view.width / 4, view.height / 4 );

						DrawLightPassFullscreen( lightTypes[i].pMatVolumeFullscreen,
						                         view.width / 4, view.height / 4,
						                         view.width, view.height );

						pRenderContext->PopRenderTargetAndViewport();

						iPassesVolumetrics[1]++;
					}
					FOR_EACH_VEC_FAST_END

					volumeLights.RemoveAll();
				}

				lightsShadowedCookied.RemoveMultipleFromHead( drawShadowedCookied );
				lightsShadowed.RemoveMultipleFromHead( drawShadowed );
				lightsCookied.RemoveMultipleFromHead( drawCookied );
				lightsSimple.RemoveMultipleFromHead( drawSimple );
			}
		}
	}

	for ( int i = 0; i < iNumLightTypes; i++ )
	{
#if DEFCFG_EXTRA_SORT
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->Bind( lightTypes[i].pMatPassWorld );
		pRenderContext->MatrixMode( MATERIAL_MODEL );

		FOR_EACH_VEC_FAST( def_light_t*, (*(lightTypes[i].lightVecWorldSimple)), l )
		{
			if ( l->pMesh_World == NULL )
				continue;

			const bool bCookie = l->HasCookie() && l->IsCookieReady();

			int numRows = bCookie ? lightTypes[i].constCount_advanced : lightTypes[i].constCount_simple;

			int memToAlloc = 4 * numRows;

			Assert( memToAlloc > 0 );

			lightDataCommon_t *pFlLightDataBlock = new lightDataCommon_t( memToAlloc );

			WriteLight( l, pFlLightDataBlock->pFlData, 0, bCookie ? 1 : 0 );

			pFlLightDataBlock->numRows = lightTypes[i].constCount_simple;
			pFlLightDataBlock->numShadowedCookied = 0;
			pFlLightDataBlock->numShadowed = 0;
			pFlLightDataBlock->numCookied = bCookie ? 1 : 0;
			pFlLightDataBlock->numSimple = 0;
			QUEUE_FIRE( CommitLightData_Common, pFlLightDataBlock );

			if ( !bCookie )
				iDrawnSimple++;

			pRenderContext->PushMatrix();
			pRenderContext->LoadMatrix( l->worldTransform );
			l->pMesh_World->Draw();
			pRenderContext->PopMatrix();

			//ShaderStencilState_t stencilLightVolume;
		//set to reference 1 where draw succeeds
		//pRenderContext->Bind( lightTypes[i].pMatPassWorldStencilStage );
		//pMatPassWorldStencilStage only draws the backface where depth test fails
		//pRenderContext->SetStencilState( stencilLightVolume );
		//l->pMesh_World->Draw();
		//set to only draw where stencil is reference 1, set depth test to nearer
			//pRenderContext->SetStencilState( stencilLightVolume );
		}
		FOR_EACH_VEC_FAST_END

		FOR_EACH_VEC_FAST( def_light_t*, (*(lightTypes[i].lightVecWorldAdvanced)), l )
		{
			if ( l->pMesh_World == NULL )
				continue;

			const bool bShadow = l->ShouldRenderShadow();
			const bool bCookie = l->HasCookie() && l->IsCookieReady();
			const bool bUber = l->HasUberlight();

			const bool bAdvanced = ( bShadow || bCookie || bUber );

			int numRows = bAdvanced ? lightTypes[i].constCount_advanced : lightTypes[i].constCount_simple;
			int memToAlloc = 4 * numRows;

			Assert( memToAlloc > 0 );

			lightDataCommon_t *pFlLightDataBlock = new lightDataCommon_t( memToAlloc );

			WriteLight( l, pFlLightDataBlock->pFlData, bShadow ? 1 : 0, bCookie ? 1 : 0 );

			if ( bShadow )
			{
				pCaller->DrawLightShadowView( view, 0, l );

				if ( l->IsSpot() )
				{
					if ( l->IsStatic() && l->HasValidAtlasSlot() )
					{
						QUEUE_FIRE( CommitTexture_ProjectedDepth, 0, GetShadowColorRT_Static( l->m_nCachedAtlasSlot ) );
					}
					else
					{
						QUEUE_FIRE( CommitTexture_ProjectedDepth, 0, GetShadowColorRT_Proj( 0 ) );
					}
				}
				else if ( l->IsPoint() )
				{
					if ( l->IsStatic() && l->HasValidAtlasSlot() )
					{
						QUEUE_FIRE( CommitTexture_DualParaboloidDepth, 0, GetShadowColorRT_StaticPoint( l->m_nCachedAtlasSlot ) );
					}
					else
					{
						QUEUE_FIRE( CommitTexture_DualParaboloidDepth, 0, GetShadowColorRT_DP( 0 ) );
					}
				}

				iDrawnShadowed++;
			}

			pFlLightDataBlock->numRows = numRows;
			pFlLightDataBlock->numShadowedCookied = ( bShadow && bCookie ) ? 1 : 0;
			pFlLightDataBlock->numShadowed = ( bShadow && !bCookie ) ? 1 : 0;
			pFlLightDataBlock->numCookied = ( !bShadow && bCookie ) ? 1 : 0;
			pFlLightDataBlock->numSimple = bAdvanced ? 0 : 1;
			QUEUE_FIRE( CommitLightData_Common, pFlLightDataBlock );

			if ( bCookie )
			{
				ITexture* pCookie = l->GetCookieForDraw();
				Assert( pCookie != NULL );
				QUEUE_FIRE( CommitTexture_Cookie, 0, pCookie );

				iDrawnCookied++;
			}

			if ( !bShadow && !bCookie )
				iDrawnSimple++;

			pRenderContext->MatrixMode( MATERIAL_MODEL );
			pRenderContext->PushMatrix();
			pRenderContext->LoadMatrix( l->worldTransform );
			pRenderContext->Bind( lightTypes[i].pMatPassWorld );

			//ShaderStencilState_t stencilLightVolume;
		//set to reference 1 where draw succeeds
		//pRenderContext->Bind( lightTypes[i].pMatPassWorldStencilStage );
		//pMatPassWorldStencilStage only draws the backface where depth test fails
		//pRenderContext->SetStencilState( stencilLightVolume );
		//l->pMesh_World->Draw();
		//set to only draw where stencil is reference 1, set depth test to nearer
			//pRenderContext->SetStencilState( stencilLightVolume );

			l->pMesh_World->Draw();

			const bool bVolumetrics = l->HasVolumetrics() && bShadow && ( r_deferred_volumetric_intensity.GetFloat() > 0.0f );
			if ( bVolumetrics )
			{
				Assert( m_bDrawVolumetrics );
				Assert( l->pMesh_Volumetrics != NULL );

				volumeData_t data;
				data.iDataOffset    = 0;
				data.iSamplerOffset = 0;
				data.iNumRows       = lightTypes[i].constCount_advanced;
				data.bHasCookie     = bCookie;
#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
				data.iSamples = l->iVolumeSamples;
#endif
				QUEUE_FIRE( CommitVolumeData, data );

				DrawVolumePrepass( false, view, l );

				pRenderContext->PushRenderTargetAndViewport( pVolumBuffer0, 0, 0, view.width / 4, view.height / 4 );

				pRenderContext->Bind( lightTypes[i].pMatVolumeWorld );
				l->pMesh_Volumetrics->Draw();

				pRenderContext->PopRenderTargetAndViewport();

				iPassesVolumetrics[0]++;
			}

			pRenderContext->MatrixMode( MATERIAL_MODEL );
			pRenderContext->PopMatrix();
		}
		FOR_EACH_VEC_FAST_END

		pRenderContext.SafeRelease();
#else
		FOR_EACH_VEC_FAST( def_light_t*, (*(lightTypes[i].lightVecWorld)), l )
		{
			if ( l->pMesh_World == NULL )
				continue;

			const bool bShadow      = l->ShouldRenderShadow();
			const bool bCookie      = l->HasCookie() && l->IsCookieReady();
			const bool bUber        = l->HasUberlight();

			const bool bAdvanced = ( bShadow || bCookie || bUber );

			int numRows    = bAdvanced ? lightTypes[i].constCount_advanced : lightTypes[i].constCount_simple;
			int memToAlloc = 4 * numRows;

			Assert( memToAlloc > 0 );

			lightDataCommon_t* pFlLightDataBlock = new lightDataCommon_t( memToAlloc );

			WriteLight( l, pFlLightDataBlock->pFlData, bShadow ? 1 : 0, bCookie ? 1 : 0 );

			if ( bShadow )
			{
				pCaller->DrawLightShadowView( view, 0, l );

				if ( l->IsSpot() )
				{
					if ( l->IsStatic() && l->HasValidAtlasSlot() )
					{
						QUEUE_FIRE( CommitTexture_ProjectedDepth, 0, GetShadowColorRT_Static( l->m_nCachedAtlasSlot ) );
					}
					else
					{
						QUEUE_FIRE( CommitTexture_ProjectedDepth, 0, GetShadowColorRT_Proj( 0 ) );
					}
				}
				else if ( l->IsPoint() )
				{
					if ( l->IsStatic() && l->HasValidAtlasSlot() )
					{
						QUEUE_FIRE( CommitTexture_DualParaboloidDepth, 0, GetShadowColorRT_StaticPoint( l->m_nCachedAtlasSlot ) );
					}
					else
					{
						QUEUE_FIRE( CommitTexture_DualParaboloidDepth, 0, GetShadowColorRT_DP( 0 ) );
					}
				}

				iDrawnShadowed++;
			}

			pFlLightDataBlock->numRows            = numRows;
			pFlLightDataBlock->numShadowedCookied = ( bShadow && bCookie ) ? 1 : 0;
			pFlLightDataBlock->numShadowed        = ( bShadow && !bCookie ) ? 1 : 0;
			pFlLightDataBlock->numCookied         = ( !bShadow && ( bCookie || bUber ) ) ? 1 : 0;
			pFlLightDataBlock->numSimple          = bAdvanced ? 0 : 1;
			QUEUE_FIRE( CommitLightData_Common, pFlLightDataBlock );

			if ( bCookie || ( !bShadow && bUber ) )
			{
				ITexture* pCookie = l->GetCookieForDraw();
				Assert( pCookie != NULL );
				QUEUE_FIRE( CommitTexture_Cookie, 0, pCookie );

				iDrawnCookied++;
			}

			if ( !bShadow && !bCookie )
				iDrawnSimple++;

			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->MatrixMode( MATERIAL_MODEL );
			pRenderContext->PushMatrix();
			pRenderContext->LoadMatrix( l->worldTransform );
			pRenderContext->Bind( lightTypes[i].pMatPassWorld );

			//ShaderStencilState_t stencilLightVolume;
			//set to reference 1 where draw succeeds
			//pRenderContext->Bind( lightTypes[i].pMatPassWorldStencilStage );
			//pMatPassWorldStencilStage only draws the backface where depth test fails
			//pRenderContext->SetStencilState( stencilLightVolume );
			//l->pMesh_World->Draw();
			//set to only draw where stencil is reference 1, set depth test to nearer
			//pRenderContext->SetStencilState( stencilLightVolume );

			l->pMesh_World->Draw();

			const bool bVolumetrics = l->HasVolumetrics() && bShadow && ( r_deferred_volumetric_intensity.GetFloat() > 0.0f );
			if ( bVolumetrics )
			{
				Assert( m_bDrawVolumetrics );
				Assert( l->pMesh_Volumetrics != NULL );

				volumeData_t data;
				data.iDataOffset    = 0;
				data.iSamplerOffset = 0;
				data.iNumRows       = lightTypes[i].constCount_advanced;
				data.bHasCookie     = bCookie;
#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
				data.iSamples = l->iVolumeSamples;
#endif
				QUEUE_FIRE( CommitVolumeData, data );

				DrawVolumePrepass( false, view, l );

				pRenderContext->PushRenderTargetAndViewport( pVolumBuffer0, 0, 0, view.width / 4, view.height / 4 );

				pRenderContext->Bind( lightTypes[i].pMatVolumeWorld );
				l->pMesh_Volumetrics->Draw();

				pRenderContext->PopRenderTargetAndViewport();

				iPassesVolumetrics[0]++;
			}

			pRenderContext->MatrixMode( MATERIAL_MODEL );
			pRenderContext->PopMatrix();
			pRenderContext.SafeRelease();
		}
		FOR_EACH_VEC_FAST_END
#endif
	}

	if ( r_deferred_light_stats.GetBool() && debugoverlay )
	{
		Assert( iNumLightTypes == 2 );

		const char* pszFilterName = "UNKNOWN FILTER";
		const char* pszProfile    = "DEPTH STENCIL (Hardware PCF)";

#if SHADOWMAPPING_USE_COLOR
		pszProfile = "COLOR RT (R32F)";
#endif

		switch ( SHADOWMAPPING_METHOD )
		{
		case SHADOWMAPPING_DEPTH_COLOR__RAW:
			pszFilterName = "RAW (R32F)";
			break;
		case SHADOWMAPPING_DEPTH_STENCIL__RAW:
			pszFilterName = "RAW (Hardware)";
			break;
		case SHADOWMAPPING_DEPTH_STENCIL__3X3_GAUSSIAN:
			pszFilterName = "3x3 GAUSS (Hardware PCF)";
			break;
		case SHADOWMAPPING_DEPTH_COLOR__4X4_SOFTWARE_BILINEAR_BOX:
			pszFilterName = "4x4 BOX (Shader Bilinear)";
			break;
		case SHADOWMAPPING_DEPTH_COLOR__4X4_SOFTWARE_BILINEAR_GAUSSIAN:
			pszFilterName = "4x4 GAUSS (Shader Bilinear)";
			break;
		case SHADOWMAPPING_DEPTH_COLOR__5X5_SOFTWARE_BILINEAR_GAUSSIAN:
			pszFilterName = "5x5 GAUSS (Shader Bilinear)";
			break;
		case SHADOWMAPPING_DEPTH_STENCIL__5X5_GAUSSIAN:
			pszFilterName = "5x5 GAUSS (Hardware PCF)";
			break;
		case SHADOWMAPPING_DEPTH_COLOR__PCSS_4X4_PCF_4X4:
			pszFilterName = "PCSS 4x4 + PCF 4x4";
			break;
		default:
			Assert(0); // add filter name to this switch case
		}

		extern int g_nShadowViewsSkippedLastFrame;
		extern int g_nShadowViewsRenderedLastFrame;
		const int nSlotsAssigned = GetStaticAtlasSlotCount();
		const int nPointSlotsAssigned = GetStaticPointAtlasSlotCount();

		int nLine = 0;
		auto PrintStatLine = [&]( const char *pFormat, ... )
		{
			char szText[256];
			va_list args;
			va_start( args, pFormat );
			V_vsnprintf( szText, sizeof( szText ), pFormat, args );
			va_end( args );

			debugoverlay->AddScreenTextOverlay( 0.70f, 0.35f, nLine++, 0.0f, 255, 255, 255, 255, szText );
		};

		PrintStatLine( "=== DEFERRED LIGHTING ===" );
		PrintStatLine( "Lights: %i in map, %i visible | Drawn: %i simple, %i shadow, %i cookie",
		               m_hDeferredLights.Count(), m_hRenderLights.Count(),
		               iDrawnSimple, iDrawnShadowed, iDrawnCookied );
		PrintStatLine( "  Point - world: %i, fullscreen: %i | Spot - world: %i, fullscreen: %i",
		               m_hPreSortedLights[LSORT_POINT_WORLD].Count(),
		               m_hPreSortedLights[LSORT_POINT_FULLSCREEN].Count(),
		               m_hPreSortedLights[LSORT_SPOT_WORLD].Count(),
		               m_hPreSortedLights[LSORT_SPOT_FULLSCREEN].Count() );

		PrintStatLine( "=== SHADOWING & CULLING ===" );
		PrintStatLine( "Filter: %s - %s", pszProfile, pszFilterName );

		if ( r_deferred_csm_cull_convex.GetBool() )
		{
			const int nTotalTested = g_CSMCullStats.nTested[0] + g_CSMCullStats.nTested[1];
			const int nTotalCulled = g_CSMCullStats.nCulled[0] + g_CSMCullStats.nCulled[1];
			const float flPct = ( nTotalTested > 0 ) ? ( nTotalCulled * 100.0f / (float)nTotalTested ) : 0.0f;
			const float flPct0 = ( g_CSMCullStats.nTested[0] > 0 ) ? ( g_CSMCullStats.nCulled[0] * 100.0f / (float)g_CSMCullStats.nTested[0] ) : 0.0f;
			const float flPct1 = ( g_CSMCullStats.nTested[1] > 0 ) ? ( g_CSMCullStats.nCulled[1] * 100.0f / (float)g_CSMCullStats.nTested[1] ) : 0.0f;

			PrintStatLine( "CSM Convex Culling: ON - Culled: %i / %i (%.1f%%)",
			               nTotalCulled, nTotalTested, flPct );
			PrintStatLine( "  Cascade 0: %i / %i (%.1f%%) | Cascade 1: %i / %i (%.1f%%)",
			               g_CSMCullStats.nCulled[0], g_CSMCullStats.nTested[0], flPct0,
			               g_CSMCullStats.nCulled[1], g_CSMCullStats.nTested[1], flPct1 );
		}
		else
		{
			PrintStatLine( "CSM Convex Culling: OFF" );
		}

		PrintStatLine( "Shadow Cache: %s | Views: %i cached (skipped), %i rendered",
		               r_deferred_shadow_cache.GetBool() ? "ON" : "OFF",
		               g_nShadowViewsSkippedLastFrame, g_nShadowViewsRenderedLastFrame );
		PrintStatLine( "  Atlas slots: %i / %i (spot) | %i / %i (point)",
		               nSlotsAssigned, MAX_STATIC_SHADOW_ATLAS_SLOTS,
		               nPointSlotsAssigned, MAX_STATIC_POINT_SHADOW_SLOTS );

		PrintStatLine( "=== RENDER PASSES ===" );
		PrintStatLine( "Fullscreen passes - point: %i, spot: %i (total: %i)",
		               iPassesDrawnFullscreen[0], iPassesDrawnFullscreen[1],
		               iPassesDrawnFullscreen[0] + iPassesDrawnFullscreen[1] );
		PrintStatLine( "Volumetric passes - world: %i, fullscreen: %i",
		               iPassesVolumetrics[0], iPassesVolumetrics[1] );
	}
}

void CLightingManager::DoSceneDebug()
{
	if ( !r_deferred_light_stats.GetBool() )
		return;

#if DEBUG
	if ( r_deferred_light_stats.GetInt() >= 2 )
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->ClearBuffers( false, true );

		DebugLights_Draw_DebugMeshes();
	}
#endif


}

void CLightingManager::DebugLights_Draw_Boundingboxes()
{
	FOR_EACH_VEC_FAST( def_light_t*, m_hDeferredLights, l )
	{
		const bool bVisible = m_hRenderLights.HasElement( l );

		Color cFace = bVisible ? Color( 32, 196, 64, 16 ) : Color( 196, 64, 32, 16 );
		Color cEdge = Color( cFace[0] * 0.5f, cFace[1] * 0.5f, cFace[2] * 0.5f, 255.0f );

		debugoverlay->AddBoxOverlay2( vec3_origin,
		                              l->bounds_min,
		                              l->bounds_max,
		                              vec3_angle,
		                              cFace, cEdge, -1.0f );

		debugoverlay->AddBoxOverlay2( vec3_origin,
		                              l->bounds_min_naive,
		                              l->bounds_max_naive,
		                              vec3_angle,
		                              Color( 32, 64, 96, 16 ),
		                              Color( 32, 64, 96, 255 ), -1.0f );
	}
	FOR_EACH_VEC_FAST_END
}

#if DEBUG
void CLightingManager::DebugLights_Draw_DebugMeshes()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_WIREFRAME_DEBUG ) );

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();

	FOR_EACH_VEC_FAST( def_light_t*, m_hRenderLights, l )
	{
		if ( !l->pMesh_Debug )
			continue;

		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->LoadMatrix( l->worldTransform );

		l->pMesh_Debug->Draw();

		if ( l->pMesh_Debug_Volumetrics )
			l->pMesh_Debug_Volumetrics->Draw();
	}
	FOR_EACH_VEC_FAST_END

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
}
#endif
