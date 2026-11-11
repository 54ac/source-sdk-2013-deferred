set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands globally for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for clangd / tools" FORCE)

# Configuration casing normalization
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()
if(CMAKE_BUILD_TYPE MATCHES "^[Dd][Ee][Bb][Uu][Gg]$")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)
else()
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

# Optional unity builds
option(ENABLE_UNITY_BUILD "Enable CMake unity/jumbo builds for accelerated full recompiles" OFF)
if(ENABLE_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
endif()

# Precompiled headers for game client and server
option(ENABLE_PCH "Enable precompiled headers (cbase.h) for game client and server" ON)

# Static CRT to match Source SDK conventions and prebuilt libraries
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Embedded (/Z7) avoids mspdbsrv.exe file locking under Ninja parallel compiles
if(MSVC)
    if(POLICY CMP0141)
        cmake_policy(SET CMP0141 NEW)
    endif()
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<CONFIG:Debug>:Embedded>")

    # Ensure /Z7 is applied to Debug and removed from Release
    foreach(LANG C CXX)
        string(REGEX REPLACE "/Z[iI]" "/Z7" CMAKE_${LANG}_FLAGS_DEBUG "${CMAKE_${LANG}_FLAGS_DEBUG}")
        string(REGEX REPLACE "/Z[iI7]" "" CMAKE_${LANG}_FLAGS_RELEASE "${CMAKE_${LANG}_FLAGS_RELEASE}")
    endforeach()
endif()

# Platform-specific definitions
add_compile_definitions(
    SOURCESDK
    SOURCE_HAS_FREETYPE
    DEV_BUILD
    FRAME_POINTER_OMISSION_DISABLED
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

if(WIN32)
    add_compile_definitions(WIN32 _WIN32 _WINDOWS _MBCS COMPILER_MSVC _DLL_EXT=.dll)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_definitions(PLATFORM_64BITS WIN64 _WIN64 COMPILER_MSVC64)
    else()
        add_compile_definitions(COMPILER_MSVC32)
    endif()
elseif(APPLE)
    add_compile_definitions(_OSX OSX POSIX _POSIX COMPILER_CLANG PLATFORM_64BITS _DLL_EXT=.dylib)
elseif(UNIX)
    add_compile_definitions(_LINUX LINUX POSIX _POSIX COMPILER_GCC PLATFORM_64BITS _DLL_EXT=.so)
endif()

# Compiler-specific configuration
if(MSVC)
    add_compile_options(
        /Zc:__cplusplus
        /Zc:inline
        /Gw
        /fp:fast
        /wd4316
        /wd5033
        /wd5054
        /wd5055
        /wd4577
        /wd4091
        /wd4355
        /wd4101
        /wd4828
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Ot>
        $<$<CONFIG:Release>:/GF>
        $<$<CONFIG:Release>:/Gy>
    )

    add_link_options(
        /INCREMENTAL:NO
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:Release>:/DEBUG:NONE>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )

    add_compile_definitions(
        _CRT_SECURE_NO_DEPRECATE
        _CRT_NONSTDC_NO_DEPRECATE
        _ALLOW_RUNTIME_LIBRARY_MISMATCH
        _ALLOW_ITERATOR_DEBUG_LEVEL_MISMATCH
    )
endif()

option(USE_LLD "Use LLVM lld-link for multithreaded linking" ON)
if(USE_LLD)
    find_program(LLD_LINK_EXE NAMES lld-link HINTS "$ENV{LOCALAPPDATA}/Microsoft/WinGet/Links" "${CMAKE_CURRENT_SOURCE_DIR}/src/devtools/bin")
    if(LLD_LINK_EXE)
        set(CMAKE_LINKER "${LLD_LINK_EXE}")
        set(CMAKE_LINKER_TYPE LLD)
        message(STATUS "[lld-link] Active multithreaded linker: ${LLD_LINK_EXE}")
    else()
        message(STATUS "[lld-link] lld-link not found, falling back to default MSVC link.exe")
    endif()
endif()


# CRT conflict resolution for static runtime targets
macro(source_sdk_nodefaultlib TARGET_NAME)
    if(MSVC)
        target_link_options(${TARGET_NAME} PRIVATE
            /ignore:4221
            $<$<CONFIG:Debug>:/NODEFAULTLIB:libc;/NODEFAULTLIB:libcd;/NODEFAULTLIB:libcmt;/NODEFAULTLIB:libcpmt;/NODEFAULTLIB:libcpmt1>
            $<$<NOT:$<CONFIG:Debug>>:/NODEFAULTLIB:libc;/NODEFAULTLIB:libcd;/NODEFAULTLIB:libcmtd;/NODEFAULTLIB:libcpmtd;/NODEFAULTLIB:libcpmtd0;/NODEFAULTLIB:libcpmtd1>
        )
    endif()
endmacro()

# Helper macro for Source SDK targets
function(source_sdk_target TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/public
        ${CMAKE_SOURCE_DIR}/src/public/tier0
        ${CMAKE_SOURCE_DIR}/src/public/tier1
    )
    source_sdk_nodefaultlib(${TARGET_NAME})
endfunction()

# Helper for SDK utility executables & libraries
function(source_sdk_add_tool TARGET_NAME)
    cmake_parse_arguments(TOOL "WIN32;SHARED" "" "SOURCES;INCLUDES;DEFINES;LIBRARIES" ${ARGN})
    if(TOOL_SHARED)
        add_library(${TARGET_NAME} SHARED ${TOOL_SOURCES})
    elseif(TOOL_WIN32)
        add_executable(${TARGET_NAME} WIN32 ${TOOL_SOURCES})
    else()
        add_executable(${TARGET_NAME} ${TOOL_SOURCES})
    endif()
    source_sdk_target(${TARGET_NAME})
    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Tools/SDK")
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/utils/common
        ${TOOL_INCLUDES}
    )
    if(TOOL_DEFINES)
        target_compile_definitions(${TARGET_NAME} PRIVATE ${TOOL_DEFINES})
    endif()
    target_link_libraries(${TARGET_NAME} PRIVATE
        tier0
        tier1
        tier2
        vstdlib
        mathlib
        ${TOOL_LIBRARIES}
    )
endfunction()
