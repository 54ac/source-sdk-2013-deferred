cmake_minimum_required(VERSION 4.0)

# Guard against CMake picking up MinGW/MSYS2 tools in PATH when compiling with MSVC
if(WIN32)
    if(CMAKE_LINKER MATCHES "ld(\\.exe)?$")
        unset(CMAKE_LINKER CACHE)
    endif()
    if(CMAKE_AR MATCHES "ar(\\.exe)?$")
        unset(CMAKE_AR CACHE)
    endif()
    if(CMAKE_RANLIB MATCHES "ranlib(\\.exe)?$")
        unset(CMAKE_RANLIB CACHE)
    endif()
endif()

# Locate Visual Studio and Ninja dynamically if not running from VsDevCmd
if(WIN32)
    if(NOT DEFINED ENV{VSINSTALLDIR})
        find_program(VSWHERE_EXE vswhere HINTS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer")
        if(VSWHERE_EXE)
            execute_process(
                COMMAND "${VSWHERE_EXE}" -latest -property installationPath
                OUTPUT_VARIABLE _VS_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(_VS_DIR AND EXISTS "${_VS_DIR}")
                set(ENV{VSINSTALLDIR} "${_VS_DIR}")
            endif()
        endif()
    endif()
    if(NOT CMAKE_MAKE_PROGRAM)
        find_program(CMAKE_MAKE_PROGRAM ninja HINTS
            "$ENV{VSINSTALLDIR}/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"
        )
    endif()
endif()

project(SourceSdkDeferred C CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SourceSdkDefaults)

# Directory definitions
set(SRCDIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(GAME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/game")
set(GAME "hl2mp" CACHE STRING "Target game: hl2mp, hl2, episodic, tf")
set_property(CACHE GAME PROPERTY STRINGS hl2mp hl2 episodic tf)
set(GAMEDIR "${GAME_ROOT}/mod_${GAME}")
set(DEVTOOLS_BIN "${SRCDIR}/devtools/bin")
set(LIBCOMMON_DIR "${SRCDIR}/lib/common/x64/2015/release")
set(LIBPUBLIC_DIR "${SRCDIR}/lib/public/x64")

option(BUILD_TOOLS "Build SDK developer tools" ON)

# Do not compile ShaderCompile if binary already exists on disk
if(EXISTS "${DEVTOOLS_BIN}/ShaderCompile.exe")
    set(BUILD_SHADERCOMPILE OFF CACHE BOOL "Build ShaderCompile tool" FORCE)
else()
    option(BUILD_SHADERCOMPILE "Build ShaderCompile tool" ON)
endif()

option(BUILD_LIBRARIES "Build SDK static libraries" ON)
option(BUILD_SHADERS "Enable deferred shader compilation" ON)
option(AUTO_BUILD_SHADERS "Automatically run shader compile on every build" ON)
option(BUILD_ALL_LAUNCHERS "Build launchers for all mods" OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Third party libraries
if(BUILD_LIBRARIES)
    # zlib 1.2.5
    set(ZLIB_DIR "${SRCDIR}/thirdparty/zlib")
    set(ZLIB_SRCS
        "${ZLIB_DIR}/adler32.c"
        "${ZLIB_DIR}/compress.c"
        "${ZLIB_DIR}/crc32.c"
        "${ZLIB_DIR}/deflate.c"
        "${ZLIB_DIR}/gzclose.c"
        "${ZLIB_DIR}/gzlib.c"
        "${ZLIB_DIR}/gzread.c"
        "${ZLIB_DIR}/gzwrite.c"
        "${ZLIB_DIR}/infback.c"
        "${ZLIB_DIR}/inffast.c"
        "${ZLIB_DIR}/inflate.c"
        "${ZLIB_DIR}/inftrees.c"
        "${ZLIB_DIR}/trees.c"
        "${ZLIB_DIR}/uncompr.c"
        "${ZLIB_DIR}/zutil.c"
    )
    add_library(libz STATIC ${ZLIB_SRCS})
    source_sdk_target(libz)
    target_include_directories(libz PUBLIC
        "${ZLIB_DIR}"
        "${SRCDIR}/public/zlib"
    )
    target_compile_definitions(libz PRIVATE
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS;_CRT_SECURE_NO_DEPRECATE>
    )
    if(MSVC)
        target_compile_options(libz PRIVATE /TC /wd4131 /wd4244 /wd4127)
    endif()
    set_target_properties(libz PROPERTIES
        FOLDER "Thirdparty"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set_target_properties(libz PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${LIBPUBLIC_DIR}"
        )
    endif()

    # libspng
    add_library(libspng STATIC
        "${SRCDIR}/thirdparty/libspng/spng/spng.c"
        "${SRCDIR}/thirdparty/libspng/spng/spng.h"
    )
    target_include_directories(libspng PUBLIC
        "${SRCDIR}/thirdparty/libspng/spng"
    )
    target_link_libraries(libspng PRIVATE libz)
    target_compile_definitions(libspng PRIVATE
        SPNG_STATIC
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    )
    if(MSVC)
        target_compile_options(libspng PRIVATE /wd4090)
    endif()
    # Mirror binary to SDK lib folder on Windows x64 for legacy compatibility
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set_target_properties(libspng PROPERTIES
            FOLDER "Thirdparty"
            ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${LIBCOMMON_DIR}"
        )
    endif()

    # lzma (LZMA SDK + Valve wrapper)
    set(LZMA_SDK_DIR "${SRCDIR}/thirdparty/lzma/C")
    set(LZMA_SRCS
        "${SRCDIR}/utils/lzma/lzma.cpp"
        "${LZMA_SDK_DIR}/LzmaEnc.c"
        "${LZMA_SDK_DIR}/LzmaDec.c"
        "${LZMA_SDK_DIR}/LzFind.c"
    )
    add_library(lzma STATIC ${LZMA_SRCS})
    source_sdk_target(lzma)
    target_include_directories(lzma PUBLIC
        "${LZMA_SDK_DIR}"
        "${SRCDIR}/utils/lzma"
    )
    target_compile_definitions(lzma PRIVATE
        _NO_EXCEPTIONS
        _LZMA_PROB32
        _7ZIP_ST
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    )
    if(MSVC)
        target_compile_options(lzma PRIVATE /wd4131 /wd4244 /wd4127 /wd4267)
    endif()
    set_target_properties(lzma PROPERTIES
        FOLDER "Thirdparty"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set_target_properties(lzma PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${LIBCOMMON_DIR}"
        )
    endif()

    # protobuf 2.6.1
    set(PROTOBUF_DIR "${SRCDIR}/thirdparty/protobuf")
    set(PROTOBUF_SRCS
        "${PROTOBUF_DIR}/src/google/protobuf/io/coded_stream.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/stubs/common.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/descriptor.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/descriptor.pb.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/descriptor_database.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/dynamic_message.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/extension_set.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/extension_set_heavy.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/generated_message_reflection.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/generated_message_util.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/io/gzip_stream.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/io/strtod.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/compiler/importer.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/message.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/message_lite.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/stubs/once.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/stubs/atomicops_internals_x86_msvc.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/compiler/parser.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/io/printer.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/reflection_ops.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/repeated_field.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/service.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/stubs/structurally_valid.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/stubs/strutil.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/stubs/substitute.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/text_format.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/io/tokenizer.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/unknown_field_set.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/wire_format.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/wire_format_lite.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/io/zero_copy_stream.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/io/zero_copy_stream_impl.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/io/zero_copy_stream_impl_lite.cc"
        "${PROTOBUF_DIR}/src/google/protobuf/stubs/stringprintf.cc"
    )
    add_library(libprotobuf STATIC ${PROTOBUF_SRCS})
    source_sdk_target(libprotobuf)
    target_include_directories(libprotobuf PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/protobuf"
        "${PROTOBUF_DIR}/src"
    )
    target_compile_definitions(libprotobuf PRIVATE
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS;_CRT_SECURE_NO_DEPRECATE>
    )
    if(MSVC)
        target_compile_options(libprotobuf PRIVATE /wd4018 /wd4244 /wd4267 /wd4146 /wd4715)
    endif()
    set_target_properties(libprotobuf PROPERTIES
        FOLDER "Thirdparty"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
endif()

if(BUILD_SHADERCOMPILE)
    set(SHADERCOMPILE_DIR "${SRCDIR}/thirdparty/ShaderCompile")

    # Add GSL and RE2 subprojects from ShaderCompile submodules
    set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory("${SHADERCOMPILE_DIR}/shared/re2" "${CMAKE_BINARY_DIR}/thirdparty/re2" EXCLUDE_FROM_ALL)
    add_subdirectory("${SHADERCOMPILE_DIR}/shared/gsl" "${CMAKE_BINARY_DIR}/thirdparty/gsl" EXCLUDE_FROM_ALL)

    set(SHADERCOMPILE_SRCS
        "${SHADERCOMPILE_DIR}/ShaderCompile/cfgprocessor.cpp"
        "${SHADERCOMPILE_DIR}/ShaderCompile/d3dxfxc.cpp"
        "${SHADERCOMPILE_DIR}/ShaderCompile/ShaderCompile.cpp"
        "${SHADERCOMPILE_DIR}/ShaderCompile/shaderparser.cpp"
        "${SHADERCOMPILE_DIR}/ShaderCompile/utlbuffer.cpp"
    )

    add_executable(ShaderCompile ${SHADERCOMPILE_SRCS})
    set_target_properties(ShaderCompile PROPERTIES CXX_STANDARD 20)
    target_include_directories(ShaderCompile PRIVATE
        "${SHADERCOMPILE_DIR}/ShaderCompile/include"
        "${SHADERCOMPILE_DIR}/shared/re2"
        "${SHADERCOMPILE_DIR}/ShaderCompile"
    )
    target_link_libraries(ShaderCompile PRIVATE
        re2::re2
        Microsoft.GSL::GSL
        $<$<PLATFORM_ID:Windows>:d3dcompiler;dbghelp>
    )
    target_compile_definitions(ShaderCompile PRIVATE
        _ITERATOR_DEBUG_LEVEL=0
    )
    if(MSVC)
        target_compile_options(ShaderCompile PRIVATE /Zc:__cplusplus /utf-8 /wd4005)
    endif()

    # Publish directly to devtools/bin
    set_target_properties(ShaderCompile PROPERTIES
        FOLDER "Tools"
        RUNTIME_OUTPUT_DIRECTORY "${DEVTOOLS_BIN}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${DEVTOOLS_BIN}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${DEVTOOLS_BIN}"
    )
endif()

if(BUILD_SHADERS)
    set(SHADER_SCRIPT_DIR "${SRCDIR}/materialsystem/stdshaders")
    set(SHADER_LIST_FILE "${SHADER_SCRIPT_DIR}/deferred_shaders.txt")

    find_program(POWERSHELL_EXE NAMES pwsh powershell)

    if(POWERSHELL_EXE)
        if(TARGET ShaderCompile)
            set(SHADERCOMPILE_DEP ShaderCompile)
        else()
            set(SHADERCOMPILE_DEP "")
        endif()
        add_custom_target(deferred_shaders
            COMMAND ${CMAKE_COMMAND} -E echo "Compiling deferred shaders with ShaderCompile..."
            COMMAND "${POWERSHELL_EXE}" -NoLogo -ExecutionPolicy Bypass -File "${DEVTOOLS_BIN}/process_shaders.ps1" -Version 30 "${SHADER_LIST_FILE}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${GAMEDIR}/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADER_SCRIPT_DIR}/shaders" "${GAMEDIR}/shaders"
            WORKING_DIRECTORY "${SHADER_SCRIPT_DIR}"
            DEPENDS ${SHADERCOMPILE_DEP}
            COMMENT "Building deferred lighting shaders"
        )
        add_custom_target(clean_shaders
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${SHADER_SCRIPT_DIR}/include"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${SHADER_SCRIPT_DIR}/shaders"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${SHADER_SCRIPT_DIR}/fxctmp9"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${SHADER_SCRIPT_DIR}/vshtmp9"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${GAMEDIR}/shaders"
            COMMENT "Cleaning compiled shaders and generated shader headers"
        )
    endif()
endif()

# Static libraries
if(BUILD_LIBRARIES)
    # Mathlib
    set(MATHLIB_SRCS
        "${SRCDIR}/mathlib/color_conversion.cpp"
        "${SRCDIR}/mathlib/halton.cpp"
        "${SRCDIR}/mathlib/lightdesc.cpp"
        "${SRCDIR}/mathlib/mathlib_base.cpp"
        "${SRCDIR}/mathlib/powsse.cpp"
        "${SRCDIR}/mathlib/sparse_convolution_noise.cpp"
        "${SRCDIR}/mathlib/sseconst.cpp"
        "${SRCDIR}/mathlib/sse.cpp"
        "${SRCDIR}/mathlib/ssenoise.cpp"
        "${SRCDIR}/mathlib/3dnow.cpp"
        "${SRCDIR}/mathlib/anorms.cpp"
        "${SRCDIR}/mathlib/bumpvects.cpp"
        "${SRCDIR}/mathlib/IceKey.cpp"
        "${SRCDIR}/mathlib/imagequant.cpp"
        "${SRCDIR}/mathlib/polyhedron.cpp"
        "${SRCDIR}/mathlib/quantize.cpp"
        "${SRCDIR}/mathlib/randsse.cpp"
        "${SRCDIR}/mathlib/spherical.cpp"
        "${SRCDIR}/mathlib/simdvectormatrix.cpp"
        "${SRCDIR}/mathlib/vector.cpp"
        "${SRCDIR}/mathlib/vmatrix.cpp"
        "${SRCDIR}/mathlib/almostequal.cpp"
    )
    add_library(mathlib STATIC ${MATHLIB_SRCS})
    source_sdk_target(mathlib)
    set_target_properties(mathlib PROPERTIES FOLDER "Libraries")
    target_compile_definitions(mathlib PRIVATE MATHLIB_LIB)
    target_include_directories(mathlib PUBLIC "${SRCDIR}/public/mathlib")

    # Tier1
    set(TIER1_SRCS
        "${SRCDIR}/tier1/bitbuf.cpp"
        "${SRCDIR}/tier1/newbitbuf.cpp"
        "${SRCDIR}/tier1/byteswap.cpp"
        "${SRCDIR}/tier1/characterset.cpp"
        "${SRCDIR}/tier1/checksum_crc.cpp"
        "${SRCDIR}/tier1/checksum_md5.cpp"
        "${SRCDIR}/tier1/checksum_sha1.cpp"
        "${SRCDIR}/tier1/commandbuffer.cpp"
        "${SRCDIR}/tier1/convar.cpp"
        "${SRCDIR}/tier1/datamanager.cpp"
        "${SRCDIR}/tier1/diff.cpp"
        "${SRCDIR}/tier1/exprevaluator.cpp"
        "${SRCDIR}/tier1/generichash.cpp"
        "${SRCDIR}/tier1/ilocalize.cpp"
        "${SRCDIR}/tier1/interface.cpp"
        "${SRCDIR}/tier1/KeyValues.cpp"
        "${SRCDIR}/tier1/keyvaluesjson.cpp"
        "${SRCDIR}/tier1/kvpacker.cpp"
        "${SRCDIR}/tier1/lzmaDecoder.cpp"
        "${SRCDIR}/tier1/mempool.cpp"
        "${SRCDIR}/tier1/memstack.cpp"
        "${SRCDIR}/tier1/NetAdr.cpp"
        "${SRCDIR}/tier1/splitstring.cpp"
        "${SRCDIR}/tier1/processor_detect.cpp"
        "${SRCDIR}/tier1/rangecheckedvar.cpp"
        "${SRCDIR}/tier1/reliabletimer.cpp"
        "${SRCDIR}/tier1/sparsematrix.cpp"
        "${SRCDIR}/tier1/snappy.cpp"
        "${SRCDIR}/tier1/snappy-sinksource.cpp"
        "${SRCDIR}/tier1/snappy-stubs-internal.cpp"
        "${SRCDIR}/tier1/stringpool.cpp"
        "${SRCDIR}/tier1/strtools.cpp"
        "${SRCDIR}/tier1/strtools_unicode.cpp"
        "${SRCDIR}/tier1/tier1.cpp"
        "${SRCDIR}/tier1/tokenreader.cpp"
        "${SRCDIR}/tier1/uniqueid.cpp"
        "${SRCDIR}/tier1/utlbinaryblock.cpp"
        "${SRCDIR}/tier1/utlbuffer.cpp"
        "${SRCDIR}/tier1/utlbufferutil.cpp"
        "${SRCDIR}/tier1/utlstring.cpp"
        "${SRCDIR}/tier1/utlsymbol.cpp"
    )
    add_library(tier1 STATIC ${TIER1_SRCS})
    source_sdk_target(tier1)
    set_target_properties(tier1 PROPERTIES FOLDER "Libraries")
    target_compile_definitions(tier1 PRIVATE TIER1_STATIC_LIB)
    target_include_directories(tier1 PUBLIC "${LZMA_SDK_DIR}")
    if(WIN32)
        target_link_libraries(tier1 PUBLIC rpcrt4 lzma)
    endif()

    add_library(raytrace STATIC
        "${SRCDIR}/raytrace/raytrace.cpp"
        "${SRCDIR}/raytrace/trace2.cpp"
        "${SRCDIR}/raytrace/trace3.cpp"
    )
    source_sdk_target(raytrace)
    set_target_properties(raytrace PROPERTIES FOLDER "Libraries")
    target_include_directories(raytrace PRIVATE "${SRCDIR}/utils/common")

    add_subdirectory("${SRCDIR}/vgui2" "${CMAKE_BINARY_DIR}/vgui2")

    add_subdirectory("${SRCDIR}/fgdlib" "${CMAKE_BINARY_DIR}/fgdlib")

    add_subdirectory("${SRCDIR}/game/client" "${CMAKE_BINARY_DIR}/game/client")
    add_subdirectory("${SRCDIR}/game/server" "${CMAKE_BINARY_DIR}/game/server")
endif()

# SDK developer tools
if(BUILD_TOOLS)
    add_subdirectory("${SRCDIR}/utils" "${CMAKE_BINARY_DIR}/utils")
endif()

# Shader DLL
if(BUILD_SHADERS)
    include("${SRCDIR}/materialsystem/stdshaders/sources_shaders.cmake")

    add_library(game_shader_dx9 SHARED
        ${GAME_SHADER_SRCS}
        "${SRCDIR}/public/tier0/memoverride.cpp"
    )
    source_sdk_target(game_shader_dx9)
    target_include_directories(game_shader_dx9 PRIVATE
        "${SRCDIR}/materialsystem/stdshaders"
        "${SRCDIR}/materialsystem/stdshaders/include"
        "${SRCDIR}/materialsystem/stdshaders/fxctmp9"
        "${SRCDIR}/materialsystem/stdshaders/vshtmp9"
    )
    target_compile_definitions(game_shader_dx9 PRIVATE
        STDSHADER_DX9_DLL_EXPORT
        FAST_MATERIALVAR_ACCESS
        GAME_SHADER_DLL
        USE_ACTUAL_DX
    )
    target_link_directories(game_shader_dx9 PRIVATE "${LIBPUBLIC_DIR}")
    target_link_libraries(game_shader_dx9 PRIVATE
        mathlib
        tier1
        tier2
        tier0
        vstdlib
        shaderlib
        version
        winmm
    )
    set_target_properties(game_shader_dx9 PROPERTIES
        FOLDER "Shaders"
        OUTPUT_NAME "game_shader_dx9"
        RUNTIME_OUTPUT_DIRECTORY "${GAMEDIR}/bin/x64"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${GAMEDIR}/bin/x64"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${GAMEDIR}/bin/x64"
    )
    if(AUTO_BUILD_SHADERS AND TARGET deferred_shaders)
        add_dependencies(game_shader_dx9 deferred_shaders)
    endif()
endif()

# Launchers
if(WIN32)
    macro(add_mod_launcher target_name output_name app_id rc_file)
        add_executable(${target_name} WIN32
            "${SRCDIR}/launcher_main/main.cpp"
            "${SRCDIR}/launcher_main/${rc_file}"
        )
        source_sdk_target(${target_name})
        target_include_directories(${target_name} PRIVATE
            "${SRCDIR}/common"
            "${SRCDIR}/launcher_main"
        )
        target_compile_definitions(${target_name} PRIVATE
            MOD_LAUNCHER
            MOD_APPID=${app_id}
        )
        target_link_libraries(${target_name} PRIVATE
            shell32
            user32
            advapi32
            gdi32
            comdlg32
            ole32
        )
        set_target_properties(${target_name} PROPERTIES
            FOLDER "Launchers"
            OUTPUT_NAME "${output_name}"
            RUNTIME_OUTPUT_DIRECTORY "${GAME_ROOT}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${GAME_ROOT}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${GAME_ROOT}"
        )
    endmacro()

    if(BUILD_ALL_LAUNCHERS)
        add_mod_launcher(launcher_hl2mp "mod_hl2mp_win64" 243750 "launcher_main_mod_hl2mp.rc")
        add_mod_launcher(launcher_hl2 "mod_hl2_win64" 220 "launcher_main_mod_hl2mp.rc")
        add_mod_launcher(launcher_episodic "mod_episodic_win64" 380 "launcher_main_mod_hl2mp.rc")
        add_mod_launcher(launcher_tf "mod_tf_win64" 440 "launcher_main_mod_tf.rc")
    else()
        if(GAME STREQUAL "tf")
            add_mod_launcher(launcher_tf "mod_tf_win64" 440 "launcher_main_mod_tf.rc")
        elseif(GAME STREQUAL "hl2")
            add_mod_launcher(launcher_hl2 "mod_hl2_win64" 220 "launcher_main_mod_hl2mp.rc")
        elseif(GAME STREQUAL "episodic")
            add_mod_launcher(launcher_episodic "mod_episodic_win64" 380 "launcher_main_mod_hl2mp.rc")
        else()
            add_mod_launcher(launcher_hl2mp "mod_hl2mp_win64" 243750 "launcher_main_mod_hl2mp.rc")
        endif()
    endif()
endif()

if(WIN32)
    add_custom_target(run_game
        COMMAND "${GAME_ROOT}/mod_${GAME}_win64.exe" -game "mod_${GAME}" -windowed -novid -dev -allowdebug
        WORKING_DIRECTORY "${GAME_ROOT}"
        DEPENDS client server game_shader_dx9 launcher_${GAME}
        COMMENT "Launching mod_${GAME} in windowed developer mode..."
    )
endif()

# Sync compile_commands.json
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(sync_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
    )
endif()

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    "${GAMEDIR}/bin/x64/client.dll"
    "${GAMEDIR}/bin/x64/server.dll"
    "${GAMEDIR}/bin/x64/game_shader_dx9.dll"
    "${GAMEDIR}/shaders"
)
