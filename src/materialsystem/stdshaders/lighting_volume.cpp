
#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "lightingpass_point_ps30.inc"

BEGIN_VS_SHADER( LIGHTING_VOLUME, "Deferred Volumetric Light Pass" )
	BEGIN_SHADER_PARAMS

		SHADER_PARAM( LIGHTTYPE, SHADER_PARAM_TYPE_INTEGER, "0", "Light volume type (0 = point, 1 = spot)" )
		SHADER_PARAM( WORLDPROJECTION, SHADER_PARAM_TYPE_BOOL, "0", "Use world projection transform" )

	END_SHADER_PARAMS

	void SetupParmsLightPassVolum( lightPassParms &p )
	{
		p.iLightTypeVar = LIGHTTYPE;
		p.iWorldProjection = WORLDPROJECTION;
	}

	SHADER_INIT_PARAMS()
	{
		lightPassParms parms;
		SetupParmsLightPassVolum( parms );
		InitParmsLightPassVolum( parms, this, params );
	}

	SHADER_INIT
	{
		lightPassParms parms;
		SetupParmsLightPassVolum( parms );
		InitPassLightPassVolum( parms, this, params );
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		lightPassParms parms;
		SetupParmsLightPassVolum( parms );
		DrawPassLightPassVolum( parms, this, params, pShaderShadow, pShaderAPI, vertexCompression );
	}

END_SHADER