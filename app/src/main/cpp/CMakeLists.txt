set(CMAKE_FIND_DEBUG_MODE TRUE) # Optional: useful for debugging find_package issues
cmake_minimum_required(VERSION 3.22) # Or your Android Studio NDK default, e.g., 3.18.1

project("fromscratch")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Good practice to disable compiler-specific extensions

# Oboe configuration
# If Oboe is included as a submodule or directly in your project,
# you might point to its CMake file directly.
# Example: add_subdirectory(path/to/oboe)
# Or, if using find_package, ensure CMAKE_MODULE_PATH or oboe_DIR is set correctly
# if it's not in a standard location.
# The line below is often used if Oboe is in a known relative path or its path is passed via gradle.
# set(oboe_DIR "${CMAKE_CURRENT_SOURCE_DIR}/path/to/oboe/lib/oboe") # Adjust if needed

# This assumes Oboe's CMake config files are findable by CMake.
# If you've added Oboe as a prefab package via Gradle, this should work.
find_package(oboe CONFIG REQUIRED)

# Add your native library
# List ALL .cpp files that are part of this library.
add_library(
        scratch-emulator-lib # This is the name from System.loadLibrary()
        SHARED
        native-lib.cpp
)

# Link libraries
target_link_libraries(
        scratch-emulator-lib
        oboe::oboe  # Link against Oboe
        android     # For AAssetManager
        log         # For __android_log_print
        # Add other libraries if needed, e.g., OpenSLES for older devices if Oboe falls back
        # OpenSLES
)

# Optional: Include directories if your headers are in non-standard locations
# relative to your .cpp files. If audio_engine.h is in the same directory
# as audio_engine.cpp and jni_bridge.cpp, this might not be strictly necessary
# but is good practice.
target_include_directories(scratch-emulator-lib PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} # Allows #include "audio_engine.h"
        # Add Oboe include directory if not handled by find_package and target_link_libraries
        # ${oboe_INCLUDE_DIRS} # This variable is usually set by find_package(oboe)
)

