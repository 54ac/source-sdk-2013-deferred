# CMakeLists.txt for SDK utilities
set(TOOLS_BIN_DIR "${GAME_ROOT}/bin/x64")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${TOOLS_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${TOOLS_BIN_DIR}")
link_directories("${LIBPUBLIC_DIR}")

include(CheckIncludeFileCXX)
check_include_file_cxx("afxwin.h" HAVE_MFC)

# =============================================================================
# captioncompiler
# =============================================================================
source_sdk_add_tool(captioncompiler
    SOURCES
        ${SRCDIR}/utils/captioncompiler/captioncompiler.cpp
        ${SRCDIR}/common/compiledcaptionswap.cpp
        ${SRCDIR}/utils/common/filesystem_tools.cpp
        ${SRCDIR}/utils/common/cmdlib.cpp
        ${SRCDIR}/public/filesystem_helpers.cpp
        ${SRCDIR}/public/filesystem_init.cpp
        ${SRCDIR}/utils/common/pacifier.cpp
        ${SRCDIR}/utils/common/scriplib.cpp
        ${SRCDIR}/public/stringregistry.cpp
    INCLUDES
        ${SRCDIR}/game/shared
        ${SRCDIR}/utils/captioncompiler
    LIBRARIES
        "${LIBPUBLIC_DIR}/appframework.lib"
        tier3
)

# =============================================================================
# glview
# =============================================================================
source_sdk_add_tool(glview WIN32
    SOURCES
        ${SRCDIR}/utils/glview/glview.cpp
        ${SRCDIR}/utils/common/cmdlib.cpp
        ${SRCDIR}/public/filesystem_helpers.cpp
        ${SRCDIR}/public/filesystem_init.cpp
        ${SRCDIR}/utils/common/filesystem_tools.cpp
        ${SRCDIR}/utils/common/physdll.cpp
    DEFINES
        PROTECTED_THINGS_DISABLE
    LIBRARIES
        glu32
        odbc32
        odbccp32
        opengl32
        winmm
)

# =============================================================================
# height2normal
# =============================================================================
source_sdk_add_tool(height2normal
    SOURCES
        ${SRCDIR}/utils/height2normal/height2normal.cpp
    LIBRARIES
        bitmap
)

# =============================================================================
# motionmapper
# =============================================================================
source_sdk_add_tool(motionmapper
    SOURCES
        ${SRCDIR}/utils/common/cmdlib.cpp
        ${SRCDIR}/public/filesystem_helpers.cpp
        ${SRCDIR}/public/filesystem_init.cpp
        ${SRCDIR}/utils/common/filesystem_tools.cpp
        ${SRCDIR}/utils/motionmapper/motionmapper.cpp
        ${SRCDIR}/utils/common/scriplib.cpp
    INCLUDES
        ${SRCDIR}/Game_Shared
    DEFINES
        PROTECTED_THINGS_DISABLE
    LIBRARIES
        winmm
)

# =============================================================================
# qc_eyes
# =============================================================================
if(HAVE_MFC)
    source_sdk_add_tool(qc_eyes WIN32
        SOURCES
            ${SRCDIR}/utils/qc_eyes/QC_Eyes.cpp
            ${SRCDIR}/utils/qc_eyes/QC_EyesDlg.cpp
            ${SRCDIR}/utils/qc_eyes/StdAfx.cpp
        LIBRARIES
            windowscodecs
    )
endif()

# =============================================================================
# serverplugin_empty
# =============================================================================
source_sdk_add_tool(serverplugin_empty SHARED
    SOURCES
        ${SRCDIR}/utils/serverplugin_sample/serverplugin_bot.cpp
        ${SRCDIR}/utils/serverplugin_sample/serverplugin_empty.cpp
    INCLUDES
        ${SRCDIR}/game/server
        ${SRCDIR}/game/shared
    LIBRARIES
        odbc32
        odbccp32
)

# =============================================================================
# tgadiff
# =============================================================================
source_sdk_add_tool(tgadiff
    SOURCES
        ${SRCDIR}/utils/tgadiff/tgadiff.cpp
    LIBRARIES
        bitmap
)

# =============================================================================
# vice
# =============================================================================
source_sdk_add_tool(vice
    SOURCES
        ${SRCDIR}/utils/common/cmdlib.cpp
        ${SRCDIR}/public/filesystem_helpers.cpp
        ${SRCDIR}/public/filesystem_init.cpp
        ${SRCDIR}/utils/common/filesystem_tools.cpp
        ${SRCDIR}/utils/vice/vice.cpp
)

# =============================================================================
# vtf2tga
# =============================================================================
source_sdk_add_tool(vtf2tga
    SOURCES
        ${SRCDIR}/utils/vtf2tga/vtf2tga.cpp
    LIBRARIES
        bitmap
        vtf
)

# =============================================================================
# vtfdiff
# =============================================================================
source_sdk_add_tool(vtfdiff
    SOURCES
        ${SRCDIR}/utils/vtfdiff/vtfdiff.cpp
    LIBRARIES
        bitmap
        vtf
)

# =============================================================================
# vvis_dll
# =============================================================================
source_sdk_add_tool(vvis_dll SHARED
    SOURCES
        ${SRCDIR}/utils/common/bsplib.cpp
        ${SRCDIR}/utils/common/cmdlib.cpp
        ${SRCDIR}/public/collisionutils.cpp
        ${SRCDIR}/public/filesystem_helpers.cpp
        ${SRCDIR}/utils/vvis/flow.cpp
        ${SRCDIR}/public/loadcmdline.cpp
        ${SRCDIR}/public/lumpfiles.cpp
        ${SRCDIR}/public/filesystem_init.cpp
        ${SRCDIR}/utils/common/filesystem_tools.cpp
        ${SRCDIR}/utils/common/MySqlDatabase.cpp
        ${SRCDIR}/utils/common/pacifier.cpp
        ${SRCDIR}/public/scratchpad3d.cpp
        ${SRCDIR}/utils/common/scratchpad_helpers.cpp
        ${SRCDIR}/utils/common/scriplib.cpp
        ${SRCDIR}/utils/common/threads.cpp
        ${SRCDIR}/utils/common/tools_minidump.cpp
        ${SRCDIR}/utils/vvis/vvis.cpp
        ${SRCDIR}/utils/vvis/WaterDist.cpp
        ${SRCDIR}/public/zip_utils.cpp
    INCLUDES
        ${SRCDIR}/utils/vmpi
        ${SRCDIR}/utils/vmpi/mysql/include
    DEFINES
        PROTECTED_THINGS_DISABLE
    LIBRARIES
        lzma
        odbc32
        odbccp32
        ws2_32
)

# =============================================================================
# vvis
# =============================================================================
source_sdk_add_tool(vvis
    SOURCES
        ${SRCDIR}/utils/vvis_launcher/vvis_launcher.cpp
        ${SRCDIR}/utils/vvis_launcher/StdAfx.cpp
)

# =============================================================================
# vrad_dll
# =============================================================================
source_sdk_add_tool(vrad_dll SHARED
    SOURCES
        ${SRCDIR}/public/bsptreedata.cpp
        ${SRCDIR}/public/disp_common.cpp
        ${SRCDIR}/public/disp_powerinfo.cpp
        ${SRCDIR}/utils/vrad/disp_vrad.cpp
        ${SRCDIR}/utils/vrad/imagepacker.cpp
        ${SRCDIR}/utils/vrad/incremental.cpp
        ${SRCDIR}/utils/vrad/leaf_ambient_lighting.cpp
        ${SRCDIR}/utils/vrad/lightmap.cpp
        ${SRCDIR}/public/loadcmdline.cpp
        ${SRCDIR}/public/lumpfiles.cpp
        ${SRCDIR}/utils/vrad/macro_texture.cpp
        ${SRCDIR}/public/filesystem_init.cpp
        ${SRCDIR}/utils/common/filesystem_tools.cpp
        ${SRCDIR}/utils/common/MySqlDatabase.cpp
        ${SRCDIR}/utils/common/pacifier.cpp
        ${SRCDIR}/utils/common/physdll.cpp
        ${SRCDIR}/utils/vrad/radial.cpp
        ${SRCDIR}/utils/vrad/samplehash.cpp
        ${SRCDIR}/utils/vrad/trace.cpp
        ${SRCDIR}/utils/common/utilmatlib.cpp
        ${SRCDIR}/utils/vrad/vismat.cpp
        ${SRCDIR}/utils/vrad/vrad.cpp
        ${SRCDIR}/utils/vrad/vrad_dispcoll.cpp
        ${SRCDIR}/utils/vrad/vraddetailprops.cpp
        ${SRCDIR}/utils/vrad/vraddisps.cpp
        ${SRCDIR}/utils/vrad/vraddll.cpp
        ${SRCDIR}/utils/vrad/vradstaticprops.cpp
        ${SRCDIR}/public/zip_utils.cpp
        ${SRCDIR}/utils/common/bsplib.cpp
        ${SRCDIR}/public/builddisp.cpp
        ${SRCDIR}/public/chunkfile.cpp
        ${SRCDIR}/utils/common/cmdlib.cpp
        ${SRCDIR}/public/dispcoll_common.cpp
        ${SRCDIR}/utils/common/map_shared.cpp
        ${SRCDIR}/utils/common/polylib.cpp
        ${SRCDIR}/utils/common/scriplib.cpp
        ${SRCDIR}/utils/common/threads.cpp
        ${SRCDIR}/utils/common/tools_minidump.cpp
        ${SRCDIR}/public/collisionutils.cpp
        ${SRCDIR}/public/filesystem_helpers.cpp
        ${SRCDIR}/public/scratchpad3d.cpp
        ${SRCDIR}/public/ScratchPadUtils.cpp
    INCLUDES
        ${SRCDIR}/utils/vmpi
        ${SRCDIR}/utils/vmpi/mysql/include
        ${SRCDIR}/utils/vmpi/mysql/mysqlpp/include
    DEFINES
        PROTECTED_THINGS_DISABLE
    LIBRARIES
        lzma
        bitmap
        vtf
        raytrace
        ws2_32
)

# =============================================================================
# vrad
# =============================================================================
source_sdk_add_tool(vrad
    SOURCES
        ${SRCDIR}/utils/vrad_launcher/vrad_launcher.cpp
        ${SRCDIR}/utils/vrad_launcher/stdafx.cpp
)

# =============================================================================
# vbsp
# =============================================================================
source_sdk_add_tool(vbsp
    SOURCES
        ${SRCDIR}/utils/vbsp/boundbox.cpp
        ${SRCDIR}/utils/vbsp/brushbsp.cpp
        ${SRCDIR}/public/collisionutils.cpp
        ${SRCDIR}/utils/vbsp/csg.cpp
        ${SRCDIR}/utils/vbsp/cubemap.cpp
        ${SRCDIR}/utils/vbsp/detail.cpp
        ${SRCDIR}/utils/vbsp/detailobjects.cpp
        ${SRCDIR}/public/disp_common.cpp
        ${SRCDIR}/utils/vbsp/disp_ivp.cpp
        ${SRCDIR}/public/disp_powerinfo.cpp
        ${SRCDIR}/utils/vbsp/disp_vbsp.cpp
        ${SRCDIR}/utils/vbsp/faces.cpp
        ${SRCDIR}/utils/vbsp/glfile.cpp
        ${SRCDIR}/utils/vbsp/ivp.cpp
        ${SRCDIR}/utils/vbsp/leakfile.cpp
        ${SRCDIR}/public/loadcmdline.cpp
        ${SRCDIR}/public/lumpfiles.cpp
        ${SRCDIR}/utils/vbsp/map.cpp
        ${SRCDIR}/utils/vbsp/manifest.cpp
        ${SRCDIR}/utils/vbsp/materialpatch.cpp
        ${SRCDIR}/utils/vbsp/materialsub.cpp
        ${SRCDIR}/utils/common/mstristrip.cpp
        ${SRCDIR}/utils/vbsp/nodraw.cpp
        ${SRCDIR}/utils/vbsp/normals.cpp
        ${SRCDIR}/utils/vbsp/overlay.cpp
        ${SRCDIR}/utils/common/physdll.cpp
        ${SRCDIR}/utils/vbsp/portals.cpp
        ${SRCDIR}/utils/vbsp/prtfile.cpp
        ${SRCDIR}/public/scratchpad3d.cpp
        ${SRCDIR}/utils/common/scratchpad_helpers.cpp
        ${SRCDIR}/utils/vbsp/staticprop.cpp
        ${SRCDIR}/utils/vbsp/textures.cpp
        ${SRCDIR}/utils/vbsp/tree.cpp
        ${SRCDIR}/utils/common/utilmatlib.cpp
        ${SRCDIR}/utils/vbsp/vbsp.cpp
        ${SRCDIR}/utils/vbsp/worldvertextransitionfixup.cpp
        ${SRCDIR}/utils/vbsp/writebsp.cpp
        ${SRCDIR}/public/zip_utils.cpp
        ${SRCDIR}/utils/common/bsplib.cpp
        ${SRCDIR}/public/builddisp.cpp
        ${SRCDIR}/public/chunkfile.cpp
        ${SRCDIR}/utils/common/cmdlib.cpp
        ${SRCDIR}/public/filesystem_helpers.cpp
        ${SRCDIR}/public/filesystem_init.cpp
        ${SRCDIR}/utils/common/filesystem_tools.cpp
        ${SRCDIR}/utils/common/map_shared.cpp
        ${SRCDIR}/utils/common/pacifier.cpp
        ${SRCDIR}/utils/common/polylib.cpp
        ${SRCDIR}/utils/common/scriplib.cpp
        ${SRCDIR}/utils/common/threads.cpp
        ${SRCDIR}/utils/common/tools_minidump.cpp
    INCLUDES
        ${SRCDIR}/utils/vmpi
    DEFINES
        MACRO_MATHLIB
        PROTECTED_THINGS_DISABLE
    LIBRARIES
        lzma
        bitmap
        vtf
        fgdlib
        odbc32
        odbccp32
        winmm
        ws2_32
)
