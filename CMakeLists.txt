set(LUA_SOURCES
    lapi.c
    lauxlib.c
    lbaselib.c
    lbitlib.c
    lcode.c
    lcorolib.c
    lctype.c
    ldblib.c
    ldebug.c
    ldo.c
    ldump.c
    lfunc.c
    lgc.c
    linit.c
    liolib.c
    llex.c
    #lmathlib.c
    lmem.c
    loadlib.c
    lobject.c
    lopcodes.c
    loslib.c
    lparser.c
    lstate.c
    lstring.c
    lstrlib.c
    ltable.c
    ltablib.c
    ltests.c
    ltm.c
    lundump.c
    lvm.c
    lzio.c

    ../libfixmath/libfixmath/fix16_exp.c
    ../libfixmath/libfixmath/fix16_sqrt.c
    ../libfixmath/libfixmath/fix16_str.c
    ../libfixmath/libfixmath/fix16_trig.c
    ../libfixmath/libfixmath/fix16.c
)

SET_SOURCE_FILES_PROPERTIES(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)

add_library(picolua ${LUA_SOURCES})
target_include_directories(picolua PUBLIC "../libfixmath/libfixmath")
set_property(TARGET picolua PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)