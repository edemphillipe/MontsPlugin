# Third-party dependencies, fetched at configure time (nothing vendored in the repo).
#
#   JUCE                   AGPLv3 / commercial  - audio devices, GUI, DSP building blocks
#   NeuralAmpModelerCore   MIT                  - official NAM (.nam) inference
#   RTNeural               BSD-3-Clause         - inference engine used by AIDA-X (.json)
#
# See docs/LICENSES.md before distributing commercially.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.6
    GIT_SHALLOW    TRUE)

FetchContent_Declare(nam_core
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        v0.6.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "Dependencies/eigen"
    SOURCE_SUBDIR  _no_cmake_)   # we compile its sources ourselves

FetchContent_Declare(rtneural
    GIT_REPOSITORY https://github.com/jatinchowdhury18/RTNeural.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "modules/xsimd")

FetchContent_MakeAvailable(juce nam_core)

# ---- RTNeural: xsimd backend (keeps its Eigen copy away from NAM's Eigen) ----
set(RTNEURAL_XSIMD ON CACHE BOOL "" FORCE)
set(RTNEURAL_EIGEN OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rtneural)

# ---- NAM core as a static library ----
file(GLOB NAM_SOURCES
    "${nam_core_SOURCE_DIR}/NAM/*.cpp"
    "${nam_core_SOURCE_DIR}/NAM/*/*.cpp")
add_library(nam_core STATIC ${NAM_SOURCES})
target_include_directories(nam_core
    PUBLIC  "${nam_core_SOURCE_DIR}"
            "${nam_core_SOURCE_DIR}/NAM"
            "${nam_core_SOURCE_DIR}/Dependencies/nlohmann"
    SYSTEM PUBLIC "${nam_core_SOURCE_DIR}/Dependencies/eigen")
target_compile_definitions(nam_core PUBLIC NAM_ENABLE_A2_FAST)
if(WIN32)
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(nam_core PRIVATE /W0 /bigobj)
else()
    target_compile_options(nam_core PRIVATE -w)
endif()

set(GSP_NAM_EXAMPLE_MODELS "${nam_core_SOURCE_DIR}/example_models" CACHE INTERNAL "")
