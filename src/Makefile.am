bin_PROGRAMS = scanflash

scanflash_SOURCES = main.cpp
scanflash_SOURCES += check.cpp
scanflash_SOURCES += device.cpp
scanflash_SOURCES += error.cpp

EXTRA_scanflash_SOURCES = main.hpp
EXTRA_scanflash_SOURCES += check.hpp
EXTRA_scanflash_SOURCES += device.hpp
EXTRA_scanflash_SOURCES += error.hpp

WARNINGS = -Wall -Wextra -Wno-unused-parameter

AM_CPPFLAGS  = $(WARNINGS)

AM_CXXFLAGS  = $(DEBUG_CXXFLAGS)
