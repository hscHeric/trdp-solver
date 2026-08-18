cmake_minimum_required(VERSION 3.16)

project(trdp_solver LANGUAGES CXX)

# Usa Release por padrao em geradores de configuracao unica (Make/Ninja).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de compilacao" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

add_library(graph STATIC
 common/graph.cpp
)
target_include_directories(graph PUBLIC
 ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_compile_features(graph PUBLIC cxx_std_20)

add_library(trdp STATIC
 trdp/trdp_decoder.cpp
)
target_include_directories(trdp PUBLIC
 ${CMAKE_CURRENT_SOURCE_DIR}/trdp
)
target_compile_features(trdp PUBLIC cxx_std_20)
target_link_libraries(trdp PUBLIC graph)

add_executable(trdp_solver
 main.cpp
 external/brkga_api/population.cpp
)
target_compile_features(trdp_solver PRIVATE cxx_std_20)
target_link_libraries(trdp_solver PRIVATE trdp)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(trdp_solver PRIVATE OpenMP::OpenMP_CXX)
endif()

set(RYZEN_RELEASE_FLAGS
 -O3
 -march=native
 -mtune=native
 -funroll-loops
 -falign-functions=32
 -falign-loops=32
 -fomit-frame-pointer
)

foreach(target graph trdp trdp_solver)
  target_compile_options(${target} PRIVATE
  $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>>:${RYZEN_RELEASE_FLAGS}>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra>
 )
endforeach()

# IPO e a forma portavel do CMake de habilitar -flto na compilacao e no link.
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR LANGUAGES CXX)
if(IPO_SUPPORTED)
  set_property(TARGET graph trdp trdp_solver PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
  message(WARNING "LTO indisponivel: ${IPO_ERROR}")
endif()
