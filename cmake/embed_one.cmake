# Invoked by reblue_embed_directory through 'cmake -P'. Writes OUTPUT as a
# translation unit defining SYMBOL[] from INPUT's bytes. The size is not emitted
# here: the generated header carries it as a literal so the asset table is
# constexpr.

if(NOT DEFINED INPUT OR NOT DEFINED SYMBOL OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "embed_one.cmake requires -DINPUT, -DSYMBOL, -DOUTPUT")
endif()

file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")

file(WRITE "${OUTPUT}"
"// Generated from ${INPUT} by cmake/embed_one.cmake - do not edit.\n"
"#include <cstdint>\n"
"extern const uint8_t ${SYMBOL}[];\n"
"const uint8_t ${SYMBOL}[] = {${bytes}};\n")
