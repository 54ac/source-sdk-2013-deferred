add_library(fgdlib STATIC
    gamedata.cpp
    gdclass.cpp
    gdvar.cpp
    inputoutput.cpp
    wckeyvalues.cpp
)

source_sdk_target(fgdlib)
set_target_properties(fgdlib PROPERTIES FOLDER "Libraries")

target_include_directories(fgdlib PUBLIC
    ${SRCDIR}/public
    ${SRCDIR}/public/fgdlib
    ${SRCDIR}/public/tier0
    ${SRCDIR}/public/tier1
    ${SRCDIR}/utils/common
)
