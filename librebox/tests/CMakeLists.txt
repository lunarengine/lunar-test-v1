# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# Test executable name
set(TEST_TARGET librebox_tests)

# Test sources
set(TEST_SOURCES
  test_main.cpp
  test_instance.cpp
  test_service.cpp
)

# Create test executable
add_executable(${TEST_TARGET} ${TEST_SOURCES})

# Link with Google Test and main library components
target_link_libraries(${TEST_TARGET} 
  gtest_main 
  gtest
  ${LUAU_LIB}
)

# Include directories for tests
target_include_directories(${TEST_TARGET} PRIVATE
  ${CMAKE_SOURCE_DIR}/bootstrap
  ${LUAU_INCLUDE_DIRS}
)

# Copy test source files to build directory so tests can access them
add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  ${CMAKE_CURRENT_SOURCE_DIR}
  $<TARGET_FILE_DIR:${TEST_TARGET}>/tests
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(${TEST_TARGET})