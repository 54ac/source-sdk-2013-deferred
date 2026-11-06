#ifndef VIEWRENDER_DEFERRED_INTERNAL_H
#define VIEWRENDER_DEFERRED_INTERNAL_H

#pragma once

#include "cbase.h"
#include "viewrender_deferred.h"
#include "deferred/deferred_rt.h"
#include "deferred/clight_manager.h"
#include "deferred/cvolume_culler.h"
#include "clientleafsystem.h"
#include "vprof.h"
#include "c_rope.h"
#include "rendertexture.h"
#include "renderparm.h"
#include "viewdebug.h"
#include "glow_overlay.h"
#include "c_effects.h"
#include "clientsideeffects.h"
#include "view_scene.h"
#include "toolframework_client.h"
#include "model_types.h"
#include "view.h"
#include "viewpostprocess.h"
#include "ivieweffects.h"
#include "smoke_fog_overlay.h"
#include "clientmode_shared.h"

#include "ienginevgui.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "client_virtualreality.h"
#include "sourcevr/isourcevirtualreality.h"

#ifdef SHADEREDITOR
#include "ShaderEditor/IVShaderEditor.h"
#endif

#include "materialsystem/itexture.h"
#include "materialsystem/imaterialvar.h"
#include "tier1/callqueue.h"

#include "vgui_int.h"
#include "vgui/IPanel.h"
#include "vgui_controls/Controls.h"
#include "debugoverlay_shared.h"

#include "tier0/memdbgon.h"

extern ConVar mat_viewportupscale;
extern ConVar mat_viewportscale;

extern view_id_t g_CurrentViewID;

extern void MaybeInvalidateLocalPlayerAnimation();
extern bool DoesViewPlaneIntersectWater( float waterZ, int leafWaterDataID );
extern void SetupCurrentView( const Vector &vecOrigin, const QAngle &angles, view_id_t viewID );
extern void SetClearColorToFogColor();
extern void FinishCurrentView();
extern void GetSkyboxFogColor( float *pColor );
extern float GetSkyboxFogStart( void );
extern float GetSkyboxFogEnd( void );
extern float GetSkyboxFogMaxDensity( void );
extern void FlushWorldLists();


extern int g_nShadowViewsSkippedThisFrame;
extern int g_nShadowViewsRenderedThisFrame;
extern int g_nShadowViewsSkippedLastFrame;
extern int g_nShadowViewsRenderedLastFrame;

extern lightData_Global_t& GetActiveGlobalLightState();
extern void CalculateCSMDistances( const CViewSetup &view, float &flSplitDist, float &flMaxDist );

extern void DrawCube( CMeshBuilder &meshBuilder, Vector vecPos, float flRadius, float *pfl2Texcoords );

class CBaseWorldViewDeferred : public CRendering3dView
{
	DECLARE_CLASS( CBaseWorldViewDeferred, CRendering3dView );
protected:
	CBaseWorldViewDeferred(CViewRender *pMainView) : CRendering3dView( pMainView ) {}

	virtual bool	AdjustView( float waterHeight );

	virtual void	DrawSetup( float waterHeight, int nSetupFlags, float waterZAdjust, int iForceViewLeaf = -1, bool bShadowDepth = false );
	void			DrawExecute( float waterHeight, view_id_t viewID, float waterZAdjust, bool bShadowDepth = false );

	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	// BUGBUG this causes all sorts of problems
	virtual bool	ShouldCacheLists(){ return false; };

	virtual void	DrawWorldDeferred( float waterZAdjust );
	virtual void	DrawOpaqueRenderablesDeferred(bool);

protected:

	static void PushComposite();
	static void PopComposite();
};

//-----------------------------------------------------------------------------
// Draws the scene when there's no water or only cheap water
//-----------------------------------------------------------------------------
class CSimpleWorldViewDeferred : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CSimpleWorldViewDeferred, CBaseWorldViewDeferred );
public:
	CSimpleWorldViewDeferred(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

	void			Setup( const CViewSetup &view, int nClearFlags, bool bDrawSkybox, const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t& info, ViewCustomVisibility_t *pCustomVisibility = NULL );
	void			Draw();

	virtual bool	ShouldCacheLists() { return true; }

private:
	VisibleFogVolumeInfo_t m_fogInfo;
};

class CGBufferView : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CGBufferView, CBaseWorldViewDeferred );
public:
	CGBufferView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView )
	{
	}

	void			Setup( const CViewSetup &view, bool bDrewSkybox, float flWaterHeight = 0.0f, bool bClipWater = false, bool bDrawEntities = true );
	void			Draw();

	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	static void PushGBuffer( bool bInitial, float zScale = 1.0f, bool bClearDepth = true, int vx = 0, int vy = 0, int vw = 0, int vh = 0 );
	static void PopGBuffer();

private:
	VisibleFogVolumeInfo_t m_fogInfo;
	bool m_bDrewSkybox;
	float m_waterHeight;
};

class CSkyboxViewDeferred : public CGBufferView
{
	DECLARE_CLASS( CSkyboxViewDeferred, CRendering3dView );
public:
	CSkyboxViewDeferred(CViewRender *pMainView) :
		CGBufferView( pMainView ),
		m_pSky3dParams( NULL )
	  {
	  }

	bool			Setup( const CViewSetup &view, bool bGBuffer, SkyboxVisibility_t *pSkyboxVisible );
	void			Draw();

protected:

	virtual SkyboxVisibility_t	ComputeSkyboxVisibility();
	bool			GetSkyboxFogEnable();

	void			Enable3dSkyboxFog( void );
	void			DrawInternal( view_id_t iSkyBoxViewID = VIEW_3DSKY, ITexture *pRenderTarget = NULL, ITexture *pDepthTarget = NULL );

	sky3dparams_t *	PreRender3dSkyboxWorld( SkyboxVisibility_t nSkyboxVisible );
	sky3dparams_t *m_pSky3dParams;

	bool		m_bGBufferPass;
};

class CBaseShadowView : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CBaseShadowView, CBaseWorldViewDeferred );
public:
	CBaseShadowView(CViewRender *pMainView, def_light_t *pLight = NULL) : CBaseWorldViewDeferred( pMainView )
	{
		m_bOutputRadiosity = false;
		m_bHasReflectionView = false;
		m_pLight = pLight;
	};

	void			Setup( const CViewSetup &view,
						ITexture *pDepthTexture,
						ITexture *pDummyTexture,
						const CViewSetup *pReflView = NULL );
	void			SetupRadiosityTargets(
						ITexture *pAlbedoTexture,
						ITexture *pNormalTexture );

	void SetRadiosityOutputEnabled( bool bEnabled );

	void			Draw();
	virtual void	DrawSetup( float waterHeight, int nSetupFlags, float waterZAdjust, int iForceViewLeaf = -1, bool bShadowDepth = false );
	virtual bool	AdjustView( float waterHeight );
	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	virtual void	CalcShadowView() = 0;
	virtual void	CommitData(){};

	virtual int		GetShadowMode() = 0;

protected:
	def_light_t		*m_pLight;
	CViewSetup		m_CameraView;
	bool			m_bHasReflectionView;
	CViewSetup		m_ReflectionView;

private:

	ITexture *m_pDepthTexture;
	ITexture *m_pDummyTexture;
	ITexture *m_pRadAlbedoTexture;
	ITexture *m_pRadNormalTexture;
	ViewCustomVisibility_t shadowVis;

	bool m_bOutputRadiosity;
};

class COrthoShadowView : public CBaseShadowView
{
	DECLARE_CLASS( COrthoShadowView, CBaseShadowView );
public:
	COrthoShadowView(CViewRender *pMainView, const int &index)
		: CBaseShadowView( pMainView )
	{
			iCascadeIndex = index;
	}

	virtual void	CalcShadowView();
	virtual void	CommitData();
	virtual void	DrawSetup( float waterHeight, int nSetupFlags, float waterZAdjust, int iForceViewLeaf = -1, bool bShadowDepth = false );

	virtual int		GetShadowMode(){
		return DEFERRED_SHADOW_MODE_ORTHO;
	};

private:
	int iCascadeIndex;
	CCascadeCuller m_CascadeCuller;
	void PruneShadowCasters();
};

class CTetrahedralShadowView : public CBaseShadowView
{
	DECLARE_CLASS( CTetrahedralShadowView, CBaseShadowView );
public:
	CTetrahedralShadowView( CViewRender *pMainView, def_light_t *pLight, int index, int iFaceIndex )
		: CBaseShadowView( pMainView, pLight )
	{
		m_iIndex = index;
		m_iFaceIndex = iFaceIndex;
	}

	virtual void PushView( float waterHeight )
	{
		BaseClass::PushView( waterHeight );
	}
	virtual void PopView()
	{
		BaseClass::PopView();
	}

	virtual void CalcShadowView();
	virtual void CommitData();

	virtual int GetShadowMode()
	{
		return DEFERRED_SHADOW_MODE_TETRA;
	}

private:
	int m_iIndex;
	int m_iFaceIndex;
};

class CSpotLightShadowView : public CBaseShadowView
{
	DECLARE_CLASS( CSpotLightShadowView, CBaseShadowView );
public:
	CSpotLightShadowView(CViewRender *pMainView,
		def_light_t *pLight, int index )
		: CBaseShadowView( pMainView, pLight )
	{
			m_iIndex = index;
	}

	virtual void	CalcShadowView();
	virtual void	CommitData();

	virtual int		GetShadowMode(){
		return DEFERRED_SHADOW_MODE_PROJECTED;
	};

private:
	int m_iIndex;
};

//-----------------------------------------------------------------------------
// Base class for scenes with water
//-----------------------------------------------------------------------------
class CBaseWaterViewDeferred : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CBaseWaterViewDeferred, CBaseWorldViewDeferred );
public:
	CBaseWaterViewDeferred(CViewRender *pMainView) :
		CBaseWorldViewDeferred( pMainView )
	{}

protected:
	void			CalcWaterEyeAdjustments( const VisibleFogVolumeInfo_t &fogInfo, float &newWaterHeight, float &waterZAdjust, bool bSoftwareUserClipPlane );

	WaterRenderInfo_t m_waterInfo;
	float m_waterHeight;
	float m_waterZAdjust;
	bool m_bSoftwareUserClipPlane;
	VisibleFogVolumeInfo_t m_fogInfo;
};


//-----------------------------------------------------------------------------
// Scenes above water
//-----------------------------------------------------------------------------
class CAboveWaterViewDeferred : public CBaseWaterViewDeferred
{
	DECLARE_CLASS( CAboveWaterViewDeferred, CBaseWaterViewDeferred );
public:
	CAboveWaterViewDeferred(CViewRender *pMainView) :
		CBaseWaterViewDeferred( pMainView ),
		m_ReflectionView( pMainView ),
		m_RefractionView( pMainView )
	{}

	void Setup(  const CViewSetup &view, bool bDrawSkybox, const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t& waterInfo );
	void			Draw();

	class CReflectionView : public CBaseWorldViewDeferred
	{
		DECLARE_CLASS( CReflectionView, CBaseWorldViewDeferred );
	public:
		CReflectionView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ), m_flWaterHeight( 0.0f ) {}

		void Setup( const CViewSetup &view, float flWaterHeight, bool bReflectEntities );
		void Draw();

	private:
		float m_flWaterHeight;
	};

	class CRefractionView : public CBaseWorldViewDeferred
	{
		DECLARE_CLASS( CRefractionView, CBaseWorldViewDeferred );
	public:
		CRefractionView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

		void Setup();
		void Draw();

	private:
		CAboveWaterViewDeferred *GetOuter() { return GET_OUTER( CAboveWaterViewDeferred, m_RefractionView ); }
	};

	friend class CReflectionView;
	friend class CRefractionView;

	bool m_bViewIntersectsWater;

	CReflectionView m_ReflectionView;
	CRefractionView m_RefractionView;
};


//-----------------------------------------------------------------------------
// Scenes below water
//-----------------------------------------------------------------------------
class CUnderWaterViewDeferred : public CBaseWaterViewDeferred
{
	DECLARE_CLASS( CUnderWaterViewDeferred, CBaseWaterViewDeferred );
public:
	CUnderWaterViewDeferred(CViewRender *pMainView) :
		CBaseWaterViewDeferred( pMainView ),
		m_RefractionView( pMainView )
	{}

	void			Setup( const CViewSetup &view, bool bDrawSkybox, const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t& info );
	void			Draw();

	class CRefractionView : public CBaseWorldViewDeferred
	{
		DECLARE_CLASS( CRefractionView, CBaseWorldViewDeferred );
	public:
		CRefractionView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

		void Setup();
		void Draw();

	private:
		CUnderWaterViewDeferred *GetOuter() { return GET_OUTER( CUnderWaterViewDeferred, m_RefractionView ); }
	};

	friend class CRefractionView;

	bool m_bDrawSkybox; // @MULTICORE (toml 8/17/2006): remove after setup hoisted

	CRefractionView m_RefractionView;
};

#endif // VIEWRENDER_DEFERRED_INTERNAL_H
