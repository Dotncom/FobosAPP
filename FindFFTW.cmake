find_path(FFTW_INCLUDE_DIR fftw3.h
  HINTS ${CMAKE_PREFIX_PATH}
  /usr/local/include
)

find_library(FFTW_LIBRARY NAMES fftw3 fftw3f fftw3l
  HINTS ${CMAKE_PREFIX_PATH}
  /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFTW DEFAULT_MSG FFTW_LIBRARY FFTW_INCLUDE_DIR)

mark_as_advanced(FFTW_INCLUDE_DIR FFTW_LIBRARY)
