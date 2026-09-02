add_executable(filter_tests filter_tests.cpp)
target_include_directories(filter_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
set_target_properties(filter_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME filter COMMAND filter_tests)
