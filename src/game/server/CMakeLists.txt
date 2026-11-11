if(GAME STREQUAL "hl2")
    include(sources_hl2.cmake)
    set(SERVER_SOURCES ${SERVER_HL2_SOURCES})
    set(SERVER_PCH_DISABLED ${SERVER_HL2_PCH_DISABLED})
    set(GAME_SERVER_DEFINES HL2_DLL USES_SAVERESTORE)
    set(GAME_SERVER_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/hl2
    )
elseif(GAME STREQUAL "episodic")
    include(sources_episodic.cmake)
    set(SERVER_SOURCES ${SERVER_EPISODIC_SOURCES})
    set(SERVER_PCH_DISABLED ${SERVER_EPISODIC_PCH_DISABLED})
    set(GAME_SERVER_DEFINES HL2_DLL HL2_EPISODIC USES_SAVERESTORE)
    set(GAME_SERVER_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2
        ${CMAKE_CURRENT_SOURCE_DIR}/episodic
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/hl2
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/episodic
    )
elseif(GAME STREQUAL "tf")
    include(sources_tf.cmake)
    set(TF_PROTO_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/generated_proto_mod_tf/base_gcmessages.pb.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/generated_proto_mod_tf/econ_gcmessages.pb.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/generated_proto_mod_tf/steammessages.pb.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/generated_proto_mod_tf/tf_gcmessages.pb.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/generated_proto_mod_tf/tf_proto_def_messages.pb.cc
    )
    set(SERVER_SOURCES ${SERVER_TF_SOURCES} ${TF_PROTO_SRCS})
    set(SERVER_PCH_DISABLED ${SERVER_TF_PCH_DISABLED} ${TF_PROTO_SRCS})
    set(GAME_SERVER_DEFINES TF_DLL ENABLE_GC_MATCHMAKING GLOWS_ENABLE USE_DYNAMIC_ASSET_LOADING NEXT_BOT USE_NAV_MESH DISABLE_GC_CONNECTION DISABLE_IN_SOURCESDK INVENTORY_VIA_WEBAPI USES_ECON_ITEMS REPLAY_ENABLED)
    set(GAME_SERVER_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/generated_proto_mod_tf
        ${CMAKE_CURRENT_SOURCE_DIR}/tf
        ${CMAKE_CURRENT_SOURCE_DIR}/tf/vgui
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/tf
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/Multiplayer
        ${CMAKE_CURRENT_SOURCE_DIR}/../statemachine
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/hl2
        ${SRCDIR}/gcsdk/steamextra
    )
else() # default hl2mp
    include(sources_hl2mp.cmake)
    set(SERVER_SOURCES ${SERVER_HL2MP_SOURCES})
    set(SERVER_PCH_DISABLED ${SERVER_HL2MP_PCH_DISABLED})
    set(GAME_SERVER_DEFINES HL2MP HL2_DLL NEXT_BOT USE_NAV_MESH)
    set(GAME_SERVER_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2mp
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/hl2
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/hl2mp
    )
endif()

# Auto-detect added source files in mod directories (CONFIGURE_DEPENDS triggers Ninja re-run on add/remove)
set(SERVER_AUTO_SRCS "")
if(NOT GAME MATCHES "^(hl2|episodic|tf)$")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${GAME}")
        file(GLOB_RECURSE SERVER_MOD_AUTO_SRCS CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${GAME}/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/${GAME}/*.c"
        )
        list(APPEND SERVER_AUTO_SRCS ${SERVER_MOD_AUTO_SRCS})
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../shared/${GAME}")
        file(GLOB_RECURSE SERVER_SHARED_MOD_AUTO_SRCS CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/../shared/${GAME}/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../shared/${GAME}/*.c"
        )
        list(APPEND SERVER_AUTO_SRCS ${SERVER_SHARED_MOD_AUTO_SRCS})
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/custom")
    file(GLOB_RECURSE SERVER_CUSTOM_AUTO_SRCS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/custom/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/custom/*.c"
    )
    list(APPEND SERVER_AUTO_SRCS ${SERVER_CUSTOM_AUTO_SRCS})
    list(APPEND GAME_SERVER_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/custom")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../shared/custom")
    file(GLOB_RECURSE SERVER_SHARED_CUSTOM_AUTO_SRCS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../shared/custom/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../shared/custom/*.c"
    )
    list(APPEND SERVER_AUTO_SRCS ${SERVER_SHARED_CUSTOM_AUTO_SRCS})
    list(APPEND GAME_SERVER_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/../shared/custom")
endif()

if(SERVER_AUTO_SRCS)
    list(APPEND SERVER_SOURCES ${SERVER_AUTO_SRCS})
    list(REMOVE_DUPLICATES SERVER_SOURCES)
endif()

# Additional files not using precompiled header
list(APPEND SERVER_SOURCES
    "${SRCDIR}/public/tier0/memoverride.cpp"
)
list(APPEND SERVER_PCH_DISABLED
    "${SRCDIR}/common/randoverride.cpp"
    "${SRCDIR}/public/bone_setup.cpp"
    "${SRCDIR}/public/collisionutils.cpp"
    "${SRCDIR}/public/dt_send.cpp"
    "${SRCDIR}/public/dt_utlvector_common.cpp"
    "${SRCDIR}/public/dt_utlvector_send.cpp"
    "${SRCDIR}/public/interpolatortypes.cpp"
    "${SRCDIR}/public/networkstringtable_hash.cpp"
    "${SRCDIR}/public/tier0/memoverride.cpp"
)

add_library(server SHARED ${SERVER_SOURCES})
source_sdk_target(server)

target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GAME_SERVER_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/NextBot
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/econ
    ${SRCDIR}/utils/common
    ${SRCDIR}/thirdparty/protobuf/src
)

target_compile_definitions(server PRIVATE
    VPC
    _USRDLL
    DLLNAME=server
    GAME_DLL
    VECTOR
    VERSION_SAFE_STEAM_API_INTERFACES
    PROTECTED_THINGS_ENABLE
    sprintf=use_Q_snprintf_instead_of_sprintf
    strncpy=use_Q_strncpy_instead
    _snprintf=use_Q_snprintf_instead
    INCLUDED_STEAM2_USERID_STRUCTS
    fopen=dont_use_fopen
    ${GAME_SERVER_DEFINES}
)

target_link_directories(server PRIVATE "${LIBPUBLIC_DIR}")
target_link_libraries(server PRIVATE
    tier1
    mathlib
    tier0
    tier2
    tier3
    vstdlib
    choreoobjects
    dmxloader
    libprotobuf
    particles
    steam_api64
    shell32
    user32
    advapi32
    gdi32
    comdlg32
    ole32
    winmm
    rpcrt4
    ws2_32
    $<$<STREQUAL:${GAME},tf>:${LIBCOMMON_DIR}/replay_common.lib>
    $<$<STREQUAL:${GAME},tf>:gcsdk_sdk>
)

set_target_properties(server PROPERTIES
    FOLDER "Game"
    PREFIX ""
    OUTPUT_NAME "server"
    RUNTIME_OUTPUT_DIRECTORY "${GAMEDIR}/bin/x64"
    LIBRARY_OUTPUT_DIRECTORY "${GAMEDIR}/bin/x64"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Precompiled header
if(ENABLE_PCH)
    set_source_files_properties(${SERVER_PCH_DISABLED} stdafx.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    target_precompile_headers(server PRIVATE cbase.h)
endif()
