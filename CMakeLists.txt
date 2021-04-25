
cmake_minimum_required (VERSION 3.4)
project(LabSynthToy)

# Don't report that sample file installation up to date
set(CMAKE_INSTALL_MESSAGE LAZY)

set(LABSYNTHTOY_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
configure_file("${LABSYNTHTOY_ROOT}/LabSynthToy.config.h" "${LABSYNTHTOY_ROOT}/LabSynthToy.h" @ONLY)

add_subdirectory(LabSound)

if (APPLE)
    set(PLATFORM_LIBS
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework Accelerate"
        "-framework Cocoa"
        "-framework CoreAudio"
#        "-framework Metal"
#        "-framework MetalKit"
#        "-framework QuartzCore"
        )
endif()

add_executable(LabSynthToy
    TinySoundFont/tml.h
    TinySoundFont/tsf.h
    TinySoundFontNode.h
    TinySoundFontNode.cpp
    LabSoundTemplateNode.h
    LabSoundTemplateNode.cpp
    LabSynthToy.cpp)

target_link_libraries(LabSynthToy Lab::Sound ${PLATFORM_LIBS})
target_include_directories(LabSynthToy PRIVATE "${LABSYNTHTOY_ROOT}")
install(TARGETS LabSynthToy RUNTIME DESTINATION bin)

install(FILES
    "${LABSYNTHTOY_ROOT}/TinySoundFont/examples/florestan-subset.sf2"
    "${LABSYNTHTOY_ROOT}/TinySoundFont/examples/venture.mid"
    DESTINATION share/LabSynthToy)
