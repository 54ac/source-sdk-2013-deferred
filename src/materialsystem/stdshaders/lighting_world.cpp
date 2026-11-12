
#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "lightingpass_point_ps30.inc"

BEGIN_VS_SHADER( LIGHTING_WORLD, "Deferred World Direct Lighting Pass" )
	BEGIN_SHADER_PARAMS

		SHADER_PARAM( LIGHTTYPE, SHADER_PARAM_TYPE_INTEGER, "0", "Light type (0 = point, 1 = spot)" )
		SHADER_PARAM( WORLDPROJECTION, SHADER_PARAM_TYPE_BOOL, "0", "Use world projection geometry" )

	END_SHADER_PARAMS

	void SetupParmsLightPass( lightPassParms &p )
	{
		p.iLightTypeVar = LIGHTTYPE;
		p.iWorldProjection = WORLDPROJECTION;
	}

	SHADER_INIT_PARAMS()
	{
		lightPassParms parms;
		SetupParmsLightPass( parms );
		InitParmsLightPass( parms, this, params );
	}

	SHADER_INIT
	{
		lightPassParms parms;
		SetupParmsLightPass( parms );
		InitPassLightPass( parms, this, params );
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		lightPassParms parms;
		SetupParmsLightPass( parms );
		DrawPassLightPass( parms, this, params, pShaderShadow, pShaderAPI, vertexCompression );
	}

END_SHADER