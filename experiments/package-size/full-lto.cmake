# Isolated experiment hook: -DCMAKE_PROJECT_Cliptiq_INCLUDE=<this file>.
# Keep the ordinary production toolchain and flags unchanged.
if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
 message(FATAL_ERROR "This experiment requires Windows clang-cl")
endif()
# CMake normally selects ThinLTO for Clang. Full IPO may deduplicate more
# across translation units; keep RTTI, exceptions and escaped callbacks intact.
set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto=full")
set(CMAKE_CXX_COMPILE_OPTIONS_IPO "-flto=full")
