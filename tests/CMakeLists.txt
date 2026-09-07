add_executable(filter_tests filter_tests.cpp)
target_include_directories(filter_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
set_target_properties(filter_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME filter COMMAND filter_tests)

add_executable(update_tests update_tests.cpp)
target_include_directories(update_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib)
set_target_properties(update_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME update COMMAND update_tests)

add_executable(fixup_tests fixup_tests.cpp)
target_include_directories(fixup_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/lib/openvr/headers)
set_target_properties(fixup_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME fixup COMMAND fixup_tests)

add_executable(skeletal_tests skeletal_tests.cpp)
target_include_directories(skeletal_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib/openvr/headers)
set_target_properties(skeletal_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME skeletal COMMAND skeletal_tests)

add_executable(basestations_tests basestations_tests.cpp)
target_include_directories(basestations_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/lib)
set_target_properties(basestations_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME basestations COMMAND basestations_tests)
