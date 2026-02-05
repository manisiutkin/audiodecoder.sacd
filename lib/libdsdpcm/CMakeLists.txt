cmake_minimum_required(VERSION 3.5)
project(libdsdpcm)

set(CMAKE_CXX_STANDARD 20)

# Include directory paths
include_directories(${CMAKE_CURRENT_SOURCE_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}/binding)

set(SOURCES binding/dsdpcm_decoder.cpp
            binding/dsdpcm_engine.cpp)

set(HEADERS binding/dsdpcm_decoder.h
            binding/dsdpcm_engine.h
            binding/std_semaphore.h
            dsdpcm_constants.h
            dsdpcm_converter.h
            dsdpcm_converter_direct.h
            dsdpcm_converter_multistage.h
            dsdpcm_converter_user.h
            dsdpcm_filter_setup.h
            dsdpcm_fir.h
            pcmpcm_fir.h
            pcmpcm_src.h)

add_library(dsdpcm STATIC ${SOURCES} ${HEADERS})
set_property(TARGET dsdpcm PROPERTY POSITION_INDEPENDENT_CODE ON)
