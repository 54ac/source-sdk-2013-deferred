if(GAME STREQUAL "hl2")
    include(sources_hl2.cmake)
    set(CLIENT_SOURCES ${CLIENT_HL2_SOURCES})
    set(CLIENT_PCH_DISABLED ${CLIENT_HL2_PCH_DISABLED})
    set(GAME_CLIENT_DEFINES HL2_CLIENT_DLL)
    set(GAME_CLIENT_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2/elements
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/hl2
    )
elseif(GAME STREQUAL "episodic")
    include(sources_episodic.cmake)
    set(CLIENT_SOURCES ${CLIENT_EPISODIC_SOURCES})
    set(CLIENT_PCH_DISABLED ${CLIENT_EPISODIC_PCH_DISABLED})
    set(GAME_CLIENT_DEFINES HL2_CLIENT_DLL HL2_EPISODIC)
    set(GAME_CLIENT_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2/elements
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
    set(CLIENT_SOURCES ${CLIENT_TF_SOURCES} ${TF_PROTO_SRCS})
    set(CLIENT_PCH_DISABLED ${CLIENT_TF_PCH_DISABLED} ${TF_PROTO_SRCS})
    set(GAME_CLIENT_DEFINES TF_CLIENT_DLL USES_ECON_ITEMS ENABLE_GC_MATCHMAKING GLOWS_ENABLE USE_DYNAMIC_ASSET_LOADING VOTING_ENABLED NEXT_BOT DISABLE_GC_CONNECTION DISABLE_IN_SOURCESDK INVENTORY_VIA_WEBAPI REPLAY_ENABLED SPNG_STATIC)
    set(GAME_CLIENT_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/generated_proto_mod_tf
        ${CMAKE_CURRENT_SOURCE_DIR}/tf
        ${CMAKE_CURRENT_SOURCE_DIR}/tf/vgui
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/tf
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/econ
        ${CMAKE_CURRENT_SOURCE_DIR}/econ
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2/elements
        ${CMAKE_CURRENT_SOURCE_DIR}/../statemachine
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/Multiplayer
        ${SRCDIR}/gcsdk/steamextra
    )
else() # default hl2mp
    include(sources_hl2mp.cmake)
    set(CLIENT_SOURCES ${CLIENT_HL2MP_SOURCES})
    set(CLIENT_PCH_DISABLED ${CLIENT_HL2MP_PCH_DISABLED})
    set(GAME_CLIENT_DEFINES HL2MP HL2_CLIENT_DLL NEXT_BOT)
    set(GAME_CLIENT_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2mp/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2mp
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2
        ${CMAKE_CURRENT_SOURCE_DIR}/hl2/elements
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/hl2mp
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/hl2
    )
endif()

# Auto-detect added source files in mod directories (CONFIGURE_DEPENDS triggers Ninja re-run on add/remove)
set(CLIENT_AUTO_SRCS "")
if(NOT GAME MATCHES "^(hl2|episodic|tf)$")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${GAME}")
        file(GLOB_RECURSE CLIENT_MOD_AUTO_SRCS CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${GAME}/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/${GAME}/*.c"
        )
        list(APPEND CLIENT_AUTO_SRCS ${CLIENT_MOD_AUTO_SRCS})
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../shared/${GAME}")
        file(GLOB_RECURSE CLIENT_SHARED_MOD_AUTO_SRCS CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/../shared/${GAME}/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../shared/${GAME}/*.c"
        )
        list(APPEND CLIENT_AUTO_SRCS ${CLIENT_SHARED_MOD_AUTO_SRCS})
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/custom")
    file(GLOB_RECURSE CLIENT_CUSTOM_AUTO_SRCS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/custom/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/custom/*.c"
    )
    list(APPEND CLIENT_AUTO_SRCS ${CLIENT_CUSTOM_AUTO_SRCS})
    list(APPEND GAME_CLIENT_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/custom")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../shared/custom")
    file(GLOB_RECURSE CLIENT_SHARED_CUSTOM_AUTO_SRCS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../shared/custom/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../shared/custom/*.c"
    )
    list(APPEND CLIENT_AUTO_SRCS ${CLIENT_SHARED_CUSTOM_AUTO_SRCS})
    list(APPEND GAME_CLIENT_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/../shared/custom")
endif()

if(CLIENT_AUTO_SRCS)
    list(APPEND CLIENT_SOURCES ${CLIENT_AUTO_SRCS})
    list(REMOVE_DUPLICATES CLIENT_SOURCES)
endif()

# Additional files not using precompiled header
list(APPEND CLIENT_PCH_DISABLED
    "${SRCDIR}/common/compiledcaptionswap.cpp"
    "${SRCDIR}/common/language.cpp"
    "${SRCDIR}/common/randoverride.cpp"
    "${SRCDIR}/public/bone_setup.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/deferred/viewrender_deferred_water.cpp"
)

add_library(client SHARED ${CLIENT_SOURCES})
source_sdk_target(client)

target_include_directories(client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GAME_CLIENT_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/game_controls
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${SRCDIR}/vgui2/include
    ${SRCDIR}/vgui2/controls
    ${SRCDIR}/thirdparty/sixensesdk/include
    ${SRCDIR}/thirdparty/protobuf/src
)

target_compile_definitions(client PRIVATE
    VPC
    _USRDLL
    DLLNAME=client
    CLIENT_DLL
    NO_STRING_T
    VECTOR
    VERSION_SAFE_STEAM_API_INTERFACES
    PROTECTED_THINGS_ENABLE
    strncpy=use_Q_strncpy_instead
    _snprintf=use_Q_snprintf_instead
    INCLUDED_STEAM2_USERID_STRUCTS
    ${GAME_CLIENT_DEFINES}
)

target_link_directories(client PRIVATE "${LIBPUBLIC_DIR}")
target_link_libraries(client PRIVATE
    tier1
    mathlib
    vgui_controls
    matsys_controls
    tier0
    tier2
    tier3
    vstdlib
    bitmap
    choreoobjects
    dmxloader
    libz
    libprotobuf
    particles
    steam_api64
    vtf
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
    $<$<STREQUAL:${GAME},tf>:${LIBCOMMON_DIR}/jpeglib.lib>
    $<$<STREQUAL:${GAME},tf>:libspng>
)

set_target_properties(client PROPERTIES
    FOLDER "Game"
    PREFIX ""
    OUTPUT_NAME "client"
    RUNTIME_OUTPUT_DIRECTORY "${GAMEDIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${GAMEDIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Precompiled header
if(ENABLE_PCH)
    set_source_files_properties(${CLIENT_PCH_DISABLED} stdafx.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    target_precompile_headers(client PRIVATE cbase.h)
endif()

add_custom_command(TARGET client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GAMEDIR}/bin/x64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:client>" "${GAMEDIR}/bin/x64/client.dll"
    COMMENT "Deploying client.dll to ${GAMEDIR}/bin and ${GAMEDIR}/bin/x64"
)
