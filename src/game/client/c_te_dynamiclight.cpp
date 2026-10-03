//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include "c_basetempentity.h"
#include "dlight.h"
#include "iefx.h"
#include "tier1/KeyValues.h"
#include "toolframework_client.h"
#include "tier0/vprof.h"
#include "deferred/deferred_shared_common.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Dynamic Light
//-----------------------------------------------------------------------------
class C_TEDynamicLight : public C_BaseTempEntity
{
public:
	DECLARE_CLASS( C_TEDynamicLight, C_BaseTempEntity );
	DECLARE_CLIENTCLASS();

					C_TEDynamicLight( void );
	virtual			~C_TEDynamicLight( void );

	virtual void	PostDataUpdate( DataUpdateType_t updateType );

public:
	Vector			m_vecOrigin;
	float			m_fRadius;
	int				r;
	int				g;
	int				b;
	int				exponent;
	float			m_fTime;
	float			m_fDecay;
};


//-----------------------------------------------------------------------------
// Networking
//-----------------------------------------------------------------------------
IMPLEMENT_CLIENTCLASS_EVENT_DT(C_TEDynamicLight, DT_TEDynamicLight, CTEDynamicLight)
	RecvPropVector( RECVINFO(m_vecOrigin)),
	RecvPropInt( RECVINFO(r)),
	RecvPropInt( RECVINFO(g)),
	RecvPropInt( RECVINFO(b)),
	RecvPropInt( RECVINFO(exponent)),
	RecvPropFloat( RECVINFO(m_fRadius)),
	RecvPropFloat( RECVINFO(m_fTime)),
	RecvPropFloat( RECVINFO(m_fDecay)),
END_RECV_TABLE()


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
C_TEDynamicLight::C_TEDynamicLight( void )
{
	m_vecOrigin.Init();
	r = 0;
	g = 0;
	b = 0;
	exponent = 0;
	m_fRadius = 0.0;
	m_fTime = 0.0;
	m_fDecay = 0.0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
C_TEDynamicLight::~C_TEDynamicLight( void )
{
}

void TE_DynamicLight( IRecipientFilter& filter, float delay,
	const Vector* org, int r, int g, int b, int exponent, float radius, float time, float decay, int nLightIndex )
{
	if ( GetDeferredManager()->IsDeferredRenderingEnabled() && GetLightingManager() )
	{
		if ( radius > 0.0f )
		{
			float flDuration = time;
			if ( decay > 0.0f )
			{
				const float flDecayDuration = radius / decay;
				flDuration = ( flDuration > 0.0f ) ? min( flDuration, flDecayDuration ) : flDecayDuration;
			}
			flDuration = clamp( flDuration, 0.03f, 5.0f );

			float flExp = ( exponent != 0 ) ? exp2f( (float)exponent ) : 1.0f;
			float flR = clamp( ( r / 255.0f ) * flExp, 0.0f, 32.0f );
			float flG = clamp( ( g / 255.0f ) * flExp, 0.0f, 32.0f );
			float flB = clamp( ( b / 255.0f ) * flExp, 0.0f, 32.0f );

			def_light_temp_t *l = new def_light_temp_t( flDuration );
			l->ang = vec3_angle;
			l->pos = *org;
			l->col_diffuse.Init( flR, flG, flB );
			l->flRadius = radius;
			l->flFalloffPower = ( nLightIndex == LIGHT_INDEX_MUZZLEFLASH ) ? 2.0f : 1.0f;
			l->iVisible_Dist = (uint16)clamp( max( 4096.0f, radius * 3.0f ), 4096.0f, 32768.0f );
			l->iVisible_Range = (uint16)clamp( radius, 512.0f, 2048.0f );
			l->iShadow_Dist = l->iVisible_Dist / 2;
			l->iShadow_Range = l->iVisible_Range / 2;
			l->MakeDirtyAll();
			GetLightingManager()->AddTempLight( l );
		}
	}
	else
	{
		dlight_t *dl = effects->CL_AllocDlight( nLightIndex );
		if ( !dl )
			return;

		dl->origin	= *org;
		dl->radius	= radius;
		dl->color.r	= r;
		dl->color.g	= g;
		dl->color.b	= b;
		dl->color.exponent	= exponent;
		dl->die		= gpGlobals->curtime + time;
		dl->decay	= decay;
	}

	if ( ToolsEnabled() && clienttools->IsInRecordingMode() )
	{
		Color clr( r, g, b, 255 );

		KeyValues *msg = new KeyValues( "TempEntity" );

 		msg->SetInt( "te", TE_DYNAMIC_LIGHT );
 		msg->SetString( "name", "TE_DynamicLight" );
		msg->SetFloat( "time", gpGlobals->curtime );
		msg->SetFloat( "duration", time );
		msg->SetFloat( "originx", org->x );
		msg->SetFloat( "originy", org->y );
		msg->SetFloat( "originz", org->z );
		msg->SetFloat( "radius", radius );
		msg->SetFloat( "decay", decay );
		msg->SetColor( "color", clr );
 		msg->SetInt( "exponent", exponent );
 		msg->SetInt( "lightindex", nLightIndex );

		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, msg );
		msg->deleteThis();
	}
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : bool -
//-----------------------------------------------------------------------------
void C_TEDynamicLight::PostDataUpdate( DataUpdateType_t updateType )
{
	VPROF( "C_TEDynamicLight::PostDataUpdate" );

	CBroadcastRecipientFilter filter;
	TE_DynamicLight( filter, 0.0f, &m_vecOrigin, r, g, b, exponent, m_fRadius, m_fTime, m_fDecay, LIGHT_INDEX_TE_DYNAMIC );
}

void TE_DynamicLight( IRecipientFilter& filter, float delay, KeyValues *pKeyValues )
{
	Vector vecOrigin;
	vecOrigin.x = pKeyValues->GetFloat( "originx" );
	vecOrigin.y = pKeyValues->GetFloat( "originy" );
	vecOrigin.z = pKeyValues->GetFloat( "originz" );
	float flDuration = pKeyValues->GetFloat( "duration" );
	Color c = pKeyValues->GetColor( "color" );
	int nExponent = pKeyValues->GetInt( "exponent" );
	float flRadius = pKeyValues->GetFloat( "radius" );
	float flDecay = pKeyValues->GetFloat( "decay" );
 	int nLightIndex = pKeyValues->GetInt( "lightindex", LIGHT_INDEX_TE_DYNAMIC );

	TE_DynamicLight( filter, 0.0f, &vecOrigin, c.r(), c.g(), c.b(), nExponent,
		flRadius, flDuration, flDecay, nLightIndex );
}

