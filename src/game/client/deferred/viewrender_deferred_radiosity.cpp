#include "cbase.h"
#include "deferred/viewrender_deferred_internal.h"
#include "tier0/memdbgon.h"

static int GetSourceRadBufferIndex( const int index )
{
	Assert( index == 0 || index == 1 );

	const bool bFar = index == 1;
	const int iNumSteps = (bFar ? r_deferred_radiosity_propagate_far_count.GetInt() : r_deferred_radiosity_propagate_count.GetInt())
		+ (bFar ? r_deferred_radiosity_blur_far_count.GetInt() : r_deferred_radiosity_blur_count.GetInt());
	return ( iNumSteps % 2 == 0 ) ? 0 : 1;
}

void CDeferredViewRender::BeginRadiosity( const CViewSetup &view )
{
	Vector fwd;
	AngleVectors( view.angles, &fwd );

	float flAmtVertical = abs( DotProduct( fwd, Vector( 0, 0, 1 ) ) );
	flAmtVertical = RemapValClamped( flAmtVertical, 0, 1, 1, 0.5f );

	for ( int iCascade = 0; iCascade < 2; iCascade++ )
	{
		const bool bFar = iCascade == 1;
		const Vector gridSize( RADIOSITY_BUFFER_SAMPLES_XY, RADIOSITY_BUFFER_SAMPLES_XY,
								RADIOSITY_BUFFER_SAMPLES_Z );
		const Vector gridSizeHalf = gridSize / 2;
		const float gridStepSize = bFar ? RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR
			: RADIOSITY_BUFFER_GRID_STEP_SIZE_CLOSE;
		const float flGridDistance = bFar ? RADIOSITY_BUFFER_GRID_STEP_DISTANCEMULT_FAR
			: RADIOSITY_BUFFER_GRID_STEP_DISTANCEMULT_CLOSE;

		Vector vecFwd;
		AngleVectors( view.angles, &vecFwd );

		m_vecRadiosityOrigin[iCascade] = view.origin
			+ vecFwd * gridStepSize * RADIOSITY_BUFFER_SAMPLES_XY * flGridDistance * flAmtVertical;

		for ( int i = 0; i < 3; i++ )
			m_vecRadiosityOrigin[iCascade][i] -= fmod( m_vecRadiosityOrigin[iCascade][i], gridStepSize );

		m_vecRadiosityOrigin[iCascade] -= gridSizeHalf * gridStepSize;

		const int iSourceBuffer = GetSourceRadBufferIndex( iCascade );
		static int iLastSourceBuffer[2] = { iSourceBuffer, GetSourceRadBufferIndex( 1 ) };

		const int clearSizeY = RADIOSITY_BUFFER_RES_Y / 2;
		const int clearOffset = (iCascade == 1) ? clearSizeY : 0;

		CMatRenderContextPtr pRenderContext( materials );

		pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityBuffer( iSourceBuffer ), NULL,
			0, clearOffset, RADIOSITY_BUFFER_RES_X, clearSizeY );
		pRenderContext->ClearColor3ub( 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();

		if ( iLastSourceBuffer[iCascade] != iSourceBuffer )
		{
			iLastSourceBuffer[iCascade] = iSourceBuffer;

			pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityBuffer( 1 - iSourceBuffer ), NULL,
				0, clearOffset, RADIOSITY_BUFFER_RES_X, clearSizeY );
			pRenderContext->ClearColor3ub( 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
			pRenderContext->PopRenderTargetAndViewport();

			pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityNormal( 1 - iSourceBuffer ), NULL,
				0, clearOffset, RADIOSITY_BUFFER_RES_X, clearSizeY );
			pRenderContext->ClearColor3ub( 127, 127, 127 );
			pRenderContext->ClearBuffers( true, false );
			pRenderContext->PopRenderTargetAndViewport();
		}

		pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityNormal( iSourceBuffer ), NULL,
			0, clearOffset, RADIOSITY_BUFFER_RES_X, clearSizeY );
		pRenderContext->ClearColor3ub( 127, 127, 127 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
	}

	UpdateRadiosityPosition();
}

void CDeferredViewRender::UpdateRadiosityPosition()
{
	struct defData_setupRadiosity
	{
	public:
		radiosityData_t data;

		static void Fire( defData_setupRadiosity d )
		{
			GetDeferredExt()->CommitRadiosityData( d.data );
		};
	};

	radiosityData_t radSetup;
	radSetup.vecOrigin[0] = m_vecRadiosityOrigin[0];
	radSetup.vecOrigin[1] = m_vecRadiosityOrigin[1];

	QUEUE_FIRE( CommitRadiosityData, radSetup );
}

void CDeferredViewRender::PerformRadiosityGlobal( const int iRadiosityCascade, const CViewSetup &view )
{
	const int iSourceBuffer = GetSourceRadBufferIndex( iRadiosityCascade );
	const int iOffsetY = (iRadiosityCascade == 1) ? RADIOSITY_BUFFER_RES_Y/2 : 0;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RADIOSITY_CASCADE, iRadiosityCascade );

	pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityBuffer( iSourceBuffer ), NULL,
		0, iOffsetY, RADIOSITY_BUFFER_VIEWPORT_SX, RADIOSITY_BUFFER_VIEWPORT_SY );
	pRenderContext->SetRenderTargetEx( 1, GetDefRT_RadiosityNormal( iSourceBuffer ) );

	pRenderContext->Bind( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_GLOBAL ) );
	GetRadiosityScreenGrid( iRadiosityCascade )->Draw();

	pRenderContext->PopRenderTargetAndViewport();
}

void CDeferredViewRender::EndRadiosity( const CViewSetup &view )
{
	const int iNumPropagateSteps[2] = { r_deferred_radiosity_propagate_count.GetInt(),
		r_deferred_radiosity_propagate_far_count.GetInt() };
	const int iNumBlurSteps[2] = { r_deferred_radiosity_blur_count.GetInt(),
		r_deferred_radiosity_blur_far_count.GetInt() };

	IMaterial *pPropagateMat[2] = {
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_PROPAGATE_0 ),
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_PROPAGATE_1 ),
	};

	IMaterial *pBlurMat[2] = {
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_BLUR_0 ),
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_BLUR_1 ),
	};

	for ( int iCascade = 0; iCascade < 2; iCascade++ )
	{
		bool bSecondDestBuffer = GetSourceRadBufferIndex( iCascade ) == 0;
		const int iOffsetY = (iCascade==1) ? RADIOSITY_BUFFER_RES_Y / 2 : 0;

		for ( int i = 0; i < iNumPropagateSteps[iCascade]; i++ )
		{
			const int index = bSecondDestBuffer ? 1 : 0;
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityBuffer( index ), NULL,
				0, iOffsetY, RADIOSITY_BUFFER_VIEWPORT_SX, RADIOSITY_BUFFER_VIEWPORT_SY );
			pRenderContext->SetRenderTargetEx( 1, GetDefRT_RadiosityNormal( index ) );

			pRenderContext->Bind( pPropagateMat[ 1 - index ] );

			GetRadiosityScreenGrid( iCascade )->Draw();

			pRenderContext->PopRenderTargetAndViewport();
			bSecondDestBuffer = !bSecondDestBuffer;
		}

		for ( int i = 0; i < iNumBlurSteps[iCascade]; i++ )
		{
			const int index = bSecondDestBuffer ? 1 : 0;
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityBuffer( index ), NULL,
				0, iOffsetY, RADIOSITY_BUFFER_VIEWPORT_SX, RADIOSITY_BUFFER_VIEWPORT_SY );
			pRenderContext->SetRenderTargetEx( 1, GetDefRT_RadiosityNormal( index ) );

			pRenderContext->Bind( pBlurMat[ 1 - index ] );

			GetRadiosityScreenGrid( iCascade )->Draw();

			pRenderContext->PopRenderTargetAndViewport();
			bSecondDestBuffer = !bSecondDestBuffer;
		}
	}

	DrawLightPassFullscreen( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_BLEND ),
		view.width, view.height );
}

void CDeferredViewRender::DebugRadiosity( const CViewSetup &view )
{
#if 0
	Vector tmp[3] = { m_vecRadiosityOrigin[1],
		m_vecRadiosityOrigin[1],
		m_vecRadiosityOrigin[1] };

	const int directions[3][2] = {
		1, 2,
		0, 2,
		0, 1,
	};

	const Vector vecCross[3] = {
		Vector( 1, 0, 0 ),
		Vector( 0, 1, 0 ),
		Vector( 0, 0, 1 ),
	};

	const int iColors[3][3] = {
		255, 0, 0,
		0, 255, 0,
		0, 0, 255,
	};

	for ( int i = 0; i < 3; i++ )
	{
		for ( int x = 0; x < RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR; x++ )
		{
			Vector tmp2 = tmp[i];

			for ( int y = 0; y < RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR; y++ )
			{
				NDebugOverlay::Line( tmp2, tmp2 + vecCross[i] * RADIOSITY_BUFFER_GRID_STEP_DISTANCEMULT_FAR * RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR,
					iColors[i][0], iColors[i][1], iColors[i][2], true, -1 );

				tmp2[ directions[i][1] ] += RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR;
			}

			tmp[i][ directions[i][0] ] += RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR;
		}
	}
#endif

	IMaterial *pMatDbgRadGrid = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_DEBUG );

	if ( m_hRadiosityDebugMeshList[0].Count() == 0 )
	{
		for ( int iCascade = 0; iCascade < 2; iCascade++ )
		{
			const bool bFar = iCascade == 1;
			const float flGridSize = bFar ? RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR : RADIOSITY_BUFFER_GRID_STEP_SIZE_CLOSE;
			const float flCubesize = flGridSize * 0.15f;
			const Vector directions[3] = {
				Vector( flGridSize, 0, 0 ),
				Vector( 0, flGridSize, 0 ),
				Vector( 0, 0, flGridSize ),
			};

			const float flUVOffsetY = bFar ? 0.5f : 0.0f;

			int nMaxVerts, nMaxIndices;
			CMatRenderContextPtr pRenderContext( materials );
			CMeshBuilder meshBuilder;

			IMesh *pMesh = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_TEXCOORD_SIZE( 0, 2 ),
				TEXTURE_GROUP_OTHER,
				pMatDbgRadGrid );
			m_hRadiosityDebugMeshList[iCascade].AddToTail( pMesh );

			IMesh *pMeshDummy = pRenderContext->GetDynamicMesh( true, NULL, NULL, pMatDbgRadGrid );
			pRenderContext->GetMaxToRender( pMeshDummy, false, &nMaxVerts, &nMaxIndices );
			pMeshDummy->Draw();

			int nMaxCubes = nMaxIndices / 36;
			if ( nMaxCubes > nMaxVerts / 24 )
				nMaxCubes = nMaxVerts / 24;

			int nRenderRemaining = nMaxCubes;
			meshBuilder.Begin( pMesh, MATERIAL_QUADS, nMaxCubes * 6 );

			const Vector2D flUVTexelSize( 1.0f / RADIOSITY_BUFFER_RES_X,
				1.0f / RADIOSITY_BUFFER_RES_Y );
			const Vector2D flUVTexelSizeHalf = flUVTexelSize * 0.5f;
			const Vector2D flUVGridSize =
				Vector2D( RADIOSITY_UVRATIO_X, RADIOSITY_UVRATIO_Y )
				* 1.0f / RADIOSITY_BUFFER_GRIDS_PER_AXIS;

			for ( int x = 0; x < RADIOSITY_BUFFER_SAMPLES_XY; x++ )
			for ( int y = 0; y < RADIOSITY_BUFFER_SAMPLES_XY; y++ )
			for ( int z = 0; z < RADIOSITY_BUFFER_SAMPLES_Z; z++ )
			{
				if ( nRenderRemaining <= 0 )
				{
					nRenderRemaining = nMaxCubes;
					meshBuilder.End();
					pMesh = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_TEXCOORD_SIZE( 0, 2 ),
								TEXTURE_GROUP_OTHER,
								GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_DEBUG ) );
					m_hRadiosityDebugMeshList[iCascade].AddToTail( pMesh );
					meshBuilder.Begin( pMesh, MATERIAL_QUADS, nMaxCubes * 6 );
				}

				int grid_x = z % RADIOSITY_BUFFER_GRIDS_PER_AXIS;
				int grid_y = z / RADIOSITY_BUFFER_GRIDS_PER_AXIS;

				float flUV[2] = {
					grid_x * flUVGridSize.x + x * flUVTexelSize.x + flUVTexelSizeHalf.x,
					grid_y * flUVGridSize.y + y * flUVTexelSize.y + flUVTexelSizeHalf.y + flUVOffsetY,
				};

				DrawCube( meshBuilder, directions[ 0 ] * x
					+ directions[ 1 ] * y
					+ directions[ 2 ] * z,
					flCubesize,
					flUV );

				nRenderRemaining--;
			}

			if ( nRenderRemaining != nMaxCubes )
				meshBuilder.End();
		}
	}

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( pMatDbgRadGrid );

	for ( int iCascade = 0; iCascade < 2; iCascade++ )
	{
		VMatrix pos;
		pos.SetupMatrixOrgAngles( m_vecRadiosityOrigin[iCascade], vec3_angle );

		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->PushMatrix();
		pRenderContext->LoadMatrix( pos );

		for ( int i = 0; i < m_hRadiosityDebugMeshList[iCascade].Count(); i++ )
			m_hRadiosityDebugMeshList[iCascade][ i ]->Draw();

		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->PopMatrix();
	}
}


IMesh *CDeferredViewRender::GetRadiosityScreenGrid( const int iCascade )
{
	if ( m_pMesh_RadiosityScreenGrid[iCascade] == NULL )
	{
		Assert( m_pMesh_RadiosityScreenGrid[iCascade] == NULL );

		const bool bFar = iCascade == 1;

		m_pMesh_RadiosityScreenGrid[iCascade] = CreateRadiosityScreenGrid(
			Vector2D( 0, (bFar?0.5f:0) ),
			bFar ? RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR : RADIOSITY_BUFFER_GRID_STEP_SIZE_CLOSE );
	}

	Assert( m_pMesh_RadiosityScreenGrid[iCascade] != NULL );

	return m_pMesh_RadiosityScreenGrid[iCascade];
}

IMesh *CDeferredViewRender::CreateRadiosityScreenGrid( const Vector2D &vecViewportBase,
	const float flWorldStepSize )
{
	VertexFormat_t format = VERTEX_POSITION
		| VERTEX_TEXCOORD_SIZE( 0, 4 )
		| VERTEX_TEXCOORD_SIZE( 1, 4 )
		| VERTEX_TANGENT_S;

	const float flTexelGridMargin = 1.5f / RADIOSITY_BUFFER_SAMPLES_XY;
	const float flTexelHalf[2] = { 0.5f / RADIOSITY_BUFFER_VIEWPORT_SX,
		0.5f / RADIOSITY_BUFFER_VIEWPORT_SY };

	const float flLocalCoordSingle = 1.0f / RADIOSITY_BUFFER_GRIDS_PER_AXIS;
	const float flLocalCoords[4][2] = {
		0, 0,
		flLocalCoordSingle, 0,
		flLocalCoordSingle, flLocalCoordSingle,
		0, flLocalCoordSingle,
	};

	CMatRenderContextPtr pRenderContext( materials );
	IMesh *pRet = pRenderContext->CreateStaticMesh(
		format, TEXTURE_GROUP_OTHER );

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pRet, MATERIAL_QUADS,
		RADIOSITY_BUFFER_GRIDS_PER_AXIS * RADIOSITY_BUFFER_GRIDS_PER_AXIS );

	float flGridOrigins[RADIOSITY_BUFFER_SAMPLES_Z][2];
	for ( int i = 0; i < RADIOSITY_BUFFER_SAMPLES_Z; i++ )
	{
		int x = i % RADIOSITY_BUFFER_GRIDS_PER_AXIS;
		int y = i / RADIOSITY_BUFFER_GRIDS_PER_AXIS;

		flGridOrigins[i][0] = x * flLocalCoordSingle + flTexelHalf[0];
		flGridOrigins[i][1] = y * flLocalCoordSingle + flTexelHalf[1];
	}

	const float flGridSize = flWorldStepSize * RADIOSITY_BUFFER_SAMPLES_XY;
	const float flLocalGridSize[4][2] = {
		0, 0,
		flGridSize, 0,
		flGridSize, flGridSize,
		0, flGridSize,
	};
	const float flLocalGridLimits[4][2] = {
		-flTexelGridMargin, -flTexelGridMargin,
		1 + flTexelGridMargin, -flTexelGridMargin,
		1 + flTexelGridMargin, 1 + flTexelGridMargin,
		-flTexelGridMargin, 1 + flTexelGridMargin,
	};

	for ( int x = 0; x < RADIOSITY_BUFFER_GRIDS_PER_AXIS; x++ )
	{
		for ( int y = 0; y < RADIOSITY_BUFFER_GRIDS_PER_AXIS; y++ )
		{
			const int iIndexLocal = x + y * RADIOSITY_BUFFER_GRIDS_PER_AXIS;
			const int iIndicesOne[2] = { Min( RADIOSITY_BUFFER_SAMPLES_Z - 1, iIndexLocal + 1 ), Max( 0, iIndexLocal - 1 ) };

			for ( int q = 0; q < 4; q++ )
			{
				meshBuilder.Position3f(
					(x * flLocalCoordSingle + flLocalCoords[q][0]) * 2 - flLocalCoordSingle * RADIOSITY_BUFFER_GRIDS_PER_AXIS,
					flLocalCoordSingle * RADIOSITY_BUFFER_GRIDS_PER_AXIS - (y * flLocalCoordSingle + flLocalCoords[q][1]) * 2,
					0 );

				meshBuilder.TexCoord4f( 0,
					(flGridOrigins[iIndexLocal][0] + flLocalCoords[q][0]) * RADIOSITY_UVRATIO_X + vecViewportBase.x,
					(flGridOrigins[iIndexLocal][1] + flLocalCoords[q][1]) * RADIOSITY_UVRATIO_Y + vecViewportBase.y,
					flLocalGridLimits[q][0],
					flLocalGridLimits[q][1] );

				meshBuilder.TexCoord4f( 1,
					(flGridOrigins[iIndicesOne[0]][0] + flLocalCoords[q][0]) * RADIOSITY_UVRATIO_X + vecViewportBase.x,
					(flGridOrigins[iIndicesOne[0]][1] + flLocalCoords[q][1]) * RADIOSITY_UVRATIO_Y + vecViewportBase.y,
					(flGridOrigins[iIndicesOne[1]][0] + flLocalCoords[q][0]) * RADIOSITY_UVRATIO_X + vecViewportBase.x,
					(flGridOrigins[iIndicesOne[1]][1] + flLocalCoords[q][1]) * RADIOSITY_UVRATIO_Y + vecViewportBase.y );

				meshBuilder.TangentS3f( flLocalGridSize[q][0],
					flLocalGridSize[q][1],
					iIndexLocal * flWorldStepSize );

				meshBuilder.AdvanceVertex();
			}
		}
	}

	meshBuilder.End();

	return pRet;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
