cmake_minimum_required(VERSION 3.10)
project(FobosAPP)

set(CMAKE_CXX_STANDARD 17)

# Find Qt5 Widgets and Charts modules
find_package(Qt5 REQUIRED COMPONENTS Widgets Multimedia Charts)
find_package(Qt5 REQUIRED COMPONENTS Core Network)

# Find FFTW3
#find_package(FFTW3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW REQUIRED fftw3f)
#find_package(OpenGL REQUIRED)
#find_package(Qt5 REQUIRED COMPONENTS OpenGL Widgets)
# Set FFTW3 paths manually
set(FFTW3_INCLUDE_DIR "/usr/local/include")
set(FFTW3_INCLUDE_DIR "/usr/local/lib/cmake/fftw3")
set(FFTW3_LIBRARIES "/usr/lib/aarch64-linux-gnu/libfftw3f.so")
set(SHADERS_DIR "${CMAKE_SOURCE_DIR}/shaders")

find_package(OpenGL REQUIRED)

include_directories(${OPENGL_INCLUDE_DIRS})
include_directories(${FFTW3_INCLUDE_DIR})
include_directories(${FFTW3F_INCLUDE_DIR})
# Find fobos library
#find_package(FFTW3 REQUIRED COMPONENTS fftw3)
find_library(FOBOS_LIBRARY NAMES fobos PATHS /libfobos)
#link_directories(/usr/lib/aarch64-linux-gnu)

# Include directories for Qt and other dependencies
include_directories(${Qt5Widgets_INCLUDE_DIRS} ${Qt5Charts_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/src)
#include_directories(/home/admin/fftw-3.3.10/api)
#include_directories(/home/admin/fftw-3.3.10)
#include_directories(/usr/include)
#include_directories(/usr/local/include)
include_directories(/usr/local/lib)
#include_directories(/home/admin/FobosAPP/src/fftw-3.3.10)

set(SOURCES
    src/fftw3.h
    src/main.cpp
    src/main.h
    src/dataprocessor.cpp
    src/dataprocessor.h
#    src/spectrumwindow.cpp
#    src/spectrumwindow.h
	src/fft.cpp
	src/fft.h
    src/audioprocessor.cpp
    src/audioprocessor.h
    src/MyGraphWidget.cpp
    src/MyGraphWidget.h
    src/MyWaterfallWidget.cpp
    src/MyWaterfallWidget.h
src/scalewidget.cpp
src/scalewidget.h
	#src/shaders/fragment_shader.glsl
	#src/shaders/vertex_shader.glsl
#    src/udp_sender.cpp
#    src/udp_sender.h
#    src/ringbuffer.cpp
#    src/ringbuffer.h
)
#  src/ringbuffer.h
qt5_wrap_cpp(MOC_SOURCES src/main.h src/scalewidget.h src/fft.h src/dataprocessor.h src/MyWaterfallWidget.h src/audioprocessor.h src/MyGraphWidget.h)  
add_executable(FobosAPP ${SOURCES} ${MOC_SOURCES})

# Link libraries
target_link_libraries(FobosAPP Qt5::Core Qt5::Network Qt5::Widgets Qt5::Charts ${OPENGL_LIBRARIES} Qt5::Multimedia ${FFTW3_LIBRARIES} ${LIBUSB_LIBRARIES} ${FOBOS_LIBRARY})

# Install targets
install(FILES ${CMAKE_SOURCE_DIR}/fobos-sdr.rules DESTINATION /etc/udev/rules.d)
install(TARGETS FobosAPP DESTINATION /usr/local/bin)
install(FILES FobosAPP.desktop DESTINATION /usr/share/applications)
