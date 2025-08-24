# CMakeLists.txt (Project Root)
cmake_minimum_required(VERSION 3.20)
project(librebox LANGUAGES C CXX)

# MSVC runtime (/MD)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# macOS specific settings
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS deployment target")
  set(CMAKE_MACOSX_RPATH ON)
endif()

# Memory debugging options
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_MSAN "Enable MemorySanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# Testing options
option(ENABLE_TESTING "Enable unit tests with Google Test" OFF)

# Paths
set(PROJ_ROOT "${CMAKE_SOURCE_DIR}")
set(VENDOR_DIR "${PROJ_ROOT}/third_party/vendor")

# ---------- Find or Build Luau lib ----------
set(LUAU_INSTALL_DIR "${VENDOR_DIR}/luau")
find_library(LUAU_LIB
  NAMES Luau libLuau.a
  PATHS "${LUAU_INSTALL_DIR}/lib"
  NO_DEFAULT_PATH
)

if(NOT LUAU_LIB)
  message(STATUS "Pre-built Luau library not found, building from source...")
  # Build Luau from the luau subdirectory
  add_subdirectory(luau)
  set(LUAU_LIB Luau)
  set(LUAU_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/luau/Common/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/luau/Ast/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/luau/Compiler/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/luau/Config/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/luau/VM/include"
  )
else()
  message(STATUS "Found pre-built Luau library: ${LUAU_LIB}")
  set(LUAU_INCLUDE_DIRS
    "${LUAU_INSTALL_DIR}/include/luau/Common/include"
    "${LUAU_INSTALL_DIR}/include/luau/Ast/include"
    "${LUAU_INSTALL_DIR}/include/luau/Compiler/include"
    "${LUAU_INSTALL_DIR}/include/luau/Config/include"
    "${LUAU_INSTALL_DIR}/include/luau/VM/include"
  )
endif()


# ---------- Find Raylib lib ----------
set(RAYLIB_INSTALL_DIR "${VENDOR_DIR}/raylib")
find_library(RAYLIB_LIB
  NAMES raylib raylib_static libraylib.a
  PATHS "${RAYLIB_INSTALL_DIR}/lib"
  NO_DEFAULT_PATH
)

# If not found in vendor directory, try system-wide search or pkg-config
if(NOT RAYLIB_LIB)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(RAYLIB raylib)
    if(RAYLIB_FOUND)
      set(RAYLIB_LIB ${RAYLIB_LINK_LIBRARIES})
      set(RAYLIB_INCLUDE_DIR ${RAYLIB_INCLUDE_DIRS})
      set(RAYLIB_COMPILE_FLAGS ${RAYLIB_CFLAGS_OTHER})
      message(STATUS "Found system Raylib via pkg-config: ${RAYLIB_LIB}")
      message(STATUS "Raylib include dirs: ${RAYLIB_INCLUDE_DIR}")
    endif()
  endif()
  
  # Try Homebrew path on macOS
  if(APPLE AND NOT RAYLIB_LIB)
    find_library(RAYLIB_LIB
      NAMES raylib libraylib.a
      PATHS /opt/homebrew/lib /usr/local/lib
    )
    if(RAYLIB_LIB)
      find_path(RAYLIB_INCLUDE_DIR raylib.h
        PATHS /opt/homebrew/include /usr/local/include
      )
      message(STATUS "Found system Raylib: ${RAYLIB_LIB}")
      message(STATUS "Raylib include dir: ${RAYLIB_INCLUDE_DIR}")
    endif()
  endif()
  
  if(NOT RAYLIB_LIB)
    message(FATAL_ERROR "Raylib not found. Please install raylib via Homebrew (brew install raylib) or place pre-built library in ${RAYLIB_INSTALL_DIR}/lib")
  endif()
else()
  message(STATUS "Found pre-built Raylib library: ${RAYLIB_LIB}")
  set(RAYLIB_INCLUDE_DIR "${RAYLIB_INSTALL_DIR}/include")
endif()


# ---------- Engine exe ----------
file(GLOB_RECURSE ENGINE_SOURCES
  "${PROJ_ROOT}/bootstrap/*.cpp"
  "${PROJ_ROOT}/core/*.cpp"
  "${PROJ_ROOT}/services/*.cpp"
  "${PROJ_ROOT}/subsystems/*.cpp"
)
add_executable(librebox ${ENGINE_SOURCES})

target_include_directories(librebox PRIVATE
  "${PROJ_ROOT}"
  ${LUAU_INCLUDE_DIRS}
)

# Add Raylib include directory and compile flags if found
if(RAYLIB_INCLUDE_DIR)
  target_include_directories(librebox PRIVATE ${RAYLIB_INCLUDE_DIR})
endif()
if(RAYLIB_COMPILE_FLAGS)
  target_compile_options(librebox PRIVATE ${RAYLIB_COMPILE_FLAGS})
endif()

target_compile_features(librebox PRIVATE cxx_std_20)

# Sanitizer flags
set(SANITIZER_FLAGS)
set(SANITIZER_LINK_FLAGS)

if(ENABLE_ASAN)
  list(APPEND SANITIZER_FLAGS -fsanitize=address -fno-omit-frame-pointer)
  list(APPEND SANITIZER_LINK_FLAGS -fsanitize=address)
  message(STATUS "AddressSanitizer enabled")
endif()

if(ENABLE_MSAN)
  list(APPEND SANITIZER_FLAGS -fsanitize=memory -fno-omit-frame-pointer)
  list(APPEND SANITIZER_LINK_FLAGS -fsanitize=memory)
  message(STATUS "MemorySanitizer enabled")
endif()

if(ENABLE_UBSAN)
  list(APPEND SANITIZER_FLAGS -fsanitize=undefined)
  list(APPEND SANITIZER_LINK_FLAGS -fsanitize=undefined)
  message(STATUS "UndefinedBehaviorSanitizer enabled")
endif()

if(MSVC)
  target_compile_options(librebox PRIVATE /EHsc /O2 /DNOMINMAX)
elseif(APPLE)
  target_compile_options(librebox PRIVATE -O2 -Wall -Wextra)
  if(SANITIZER_FLAGS)
    target_compile_options(librebox PRIVATE ${SANITIZER_FLAGS})
  endif()
elseif(UNIX)
  target_compile_options(librebox PRIVATE -O2 -Wall -Wextra)
  if(SANITIZER_FLAGS)
    target_compile_options(librebox PRIVATE ${SANITIZER_FLAGS})
  endif()
endif()


# ---------- Linking ----------
target_link_libraries(librebox PRIVATE ${LUAU_LIB} ${RAYLIB_LIB})

# Apply sanitizer link flags
if(SANITIZER_LINK_FLAGS)
  target_link_options(librebox PRIVATE ${SANITIZER_LINK_FLAGS})
endif()

if(WIN32)
  target_link_libraries(librebox PRIVATE opengl32 gdi32 winmm user32 shell32)
elseif(APPLE)
  # macOS frameworks and libraries
  find_library(OPENGL_FRAMEWORK OpenGL)
  find_library(COCOA_FRAMEWORK Cocoa)
  find_library(IOKIT_FRAMEWORK IOKit)
  find_library(COREVIDEO_FRAMEWORK CoreVideo)
  
  target_link_libraries(librebox PRIVATE 
    ${OPENGL_FRAMEWORK} 
    ${COCOA_FRAMEWORK} 
    ${IOKIT_FRAMEWORK} 
    ${COREVIDEO_FRAMEWORK}
  )
elseif(UNIX)
  # Linux/Unix libraries
  find_package(OpenGL REQUIRED)
  target_link_libraries(librebox PRIVATE 
    OpenGL::GL 
    m 
    pthread
  )
endif()


# ---------- Installation (The new "Dist" step) ----------
install(TARGETS librebox
  RUNTIME DESTINATION .
)
set(CONTENT_SRC "${PROJ_ROOT}/content/dist")
if(EXISTS "${CONTENT_SRC}")
  install(DIRECTORY "${CONTENT_SRC}/"
    DESTINATION .
  )
endif()

# Add version resource on Windows
if(WIN32)
  target_sources(librebox PRIVATE "${PROJ_ROOT}/librebox.rc")
endif()

# Set final output name
set_target_properties(librebox PROPERTIES OUTPUT_NAME "LibreboxPlayer")

# ---------- Google Test Integration ----------
if(ENABLE_TESTING)
  # Download and configure Google Test
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  
  # For Windows: Prevent overriding the parent project's compiler/linker settings
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  
  # Enable testing
  enable_testing()
  
  # Add tests subdirectory
  add_subdirectory(tests)
endif()