add_executable(filter_tests filter_tests.cpp)
target_include_directories(filter_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
set_target_properties(filter_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME filter COMMAND filter_tests)

add_executable(update_tests update_tests.cpp)
target_include_directories(update_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib)
set_target_properties(update_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME update COMMAND update_tests)
