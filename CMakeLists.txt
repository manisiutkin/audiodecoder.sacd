cmake_minimum_required(VERSION 3.15)

project(audiodecoder.sacd)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(Iconv REQUIRED)

include_directories(${PROJECT_SOURCE_DIR}/lib/id3v2lib/include
                    ${PROJECT_SOURCE_DIR}/lib/libdsdpcm
                    ${PROJECT_SOURCE_DIR}/lib/libdsdpcm/binding
                    ${PROJECT_SOURCE_DIR}/lib/libdstdec
                    ${PROJECT_SOURCE_DIR}/lib/libdstdec/binding
                    ${PROJECT_SOURCE_DIR}/lib/libdstdec/decoder
                    ${KODI_INCLUDE_DIR}/..
                    ${ICONV_INCLUDE_DIRS}
                    ${WAVPACK_INCLUDE_DIRS})

add_subdirectory(lib/libdsdpcm)
add_subdirectory(lib/libdstdec)
add_subdirectory(lib/id3v2lib)

set(CMAKE_CXX_STANDARD 17)

set(SACD_SOURCES src/Addon.cpp
                 src/SACDAudio.cpp
                 src/Settings.cpp
                 src/udsd/dsdpcm_wrapper.cpp
                 src/udsd/id3_tagger.cpp
                 src/udsd/sacd_metabase.cpp
                 src/udsd/scarletbook.cpp
                 src/udsd/udsd_core.cpp
                 src/udsd/udsd_media.cpp
                 src/udsd/udsd_reader_dsdiff.cpp
                 src/udsd/udsd_reader_dsf.cpp
                 src/udsd/udsd_reader_sacd.cpp
                 src/udsd/udsd_utility.cpp)

set(SACD_HEADERS src/Addon.h
                 src/SACDAudio.h
                 src/Settings.h
                 src/udsd/dsdpcm_wrapper.h
                 src/udsd/id3_tagger.h
                 src/udsd/sacd_metabase.h
                 src/udsd/scarletbook.h
                 src/udsd/udsd_core.h
                 src/udsd/udsd_media.h
                 src/udsd/udsd_reader.h
                 src/udsd/udsd_reader_dsdiff.h
                 src/udsd/udsd_reader_dsf.h
                 src/udsd/udsd_reader_sacd.h
                 src/udsd/udsd_utility.h)

set(DEPLIBS dsdpcm dstdec id3v2 ${WAVPACK_LIBRARIES} ${ICONV_LIBRARY})

build_addon(audiodecoder.sacd SACD DEPLIBS)

include(CPack)
