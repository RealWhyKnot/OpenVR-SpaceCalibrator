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

add_executable(autodetect_tests autodetect_tests.cpp)
target_include_directories(autodetect_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib)
set_target_properties(autodetect_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME autodetect COMMAND autodetect_tests)

add_executable(skeletal_tests skeletal_tests.cpp)
target_include_directories(skeletal_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib/openvr/headers)
set_target_properties(skeletal_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME skeletal COMMAND skeletal_tests)

add_executable(basestations_tests basestations_tests.cpp)
target_include_directories(basestations_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/lib)
set_target_properties(basestations_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME basestations COMMAND basestations_tests)

add_executable(driverconflict_tests driverconflict_tests.cpp)
target_include_directories(driverconflict_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib)
set_target_properties(driverconflict_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME driverconflict COMMAND driverconflict_tests)

add_executable(dashboard_pointer_tests dashboard_pointer_tests.cpp)
target_include_directories(dashboard_pointer_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(dashboard_pointer_tests PRIVATE imgui)
set_target_properties(dashboard_pointer_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME dashboard_pointer COMMAND dashboard_pointer_tests)

add_executable(posehealth_tests posehealth_tests.cpp)
target_include_directories(posehealth_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/lib/openvr/headers)
set_target_properties(posehealth_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME posehealth COMMAND posehealth_tests)

add_executable(posehook_tests posehook_tests.cpp)
target_include_directories(posehook_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/driver ${CMAKE_SOURCE_DIR}/lib/openvr/headers)
set_target_properties(posehook_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME posehook COMMAND posehook_tests)

add_executable(stick_tests stick_tests.cpp)
target_include_directories(stick_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
set_target_properties(stick_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME stick COMMAND stick_tests)

add_executable(stick_hook_tests stick_hook_tests.cpp ${CMAKE_SOURCE_DIR}/src/driver/StickHook.cpp ${CMAKE_SOURCE_DIR}/src/driver/Hooking.cpp ${CMAKE_SOURCE_DIR}/src/driver/Logging.cpp)
target_include_directories(stick_hook_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/driver ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib/openvr/headers ${CMAKE_SOURCE_DIR}/lib/minhook/include)
target_link_libraries(stick_hook_tests PRIVATE minhook)
target_compile_definitions(stick_hook_tests PRIVATE NOMINMAX)
set_target_properties(stick_hook_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME stick_hook COMMAND stick_hook_tests)

set(PERF_DRIVER_SOURCES
	${CMAKE_SOURCE_DIR}/src/driver/ServerTrackedDeviceProvider.cpp
	${CMAKE_SOURCE_DIR}/src/driver/IPCServer.cpp
	${CMAKE_SOURCE_DIR}/src/driver/InterfaceHookInjector.cpp
	${CMAKE_SOURCE_DIR}/src/driver/SkeletalHook.cpp
	${CMAKE_SOURCE_DIR}/src/driver/StickHook.cpp
	${CMAKE_SOURCE_DIR}/src/driver/Hooking.cpp
	${CMAKE_SOURCE_DIR}/src/driver/Logging.cpp)
add_executable(perf_driver_tests perf_driver_tests.cpp ${PERF_DRIVER_SOURCES})
target_include_directories(perf_driver_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/driver ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib/openvr/headers ${CMAKE_SOURCE_DIR}/lib/minhook/include ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(perf_driver_tests PRIVATE minhook)
target_compile_definitions(perf_driver_tests PRIVATE NOMINMAX UNICODE _CRT_SECURE_NO_WARNINGS)
set_target_properties(perf_driver_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME perf_driver COMMAND perf_driver_tests)
set_tests_properties(perf_driver PROPERTIES LABELS perf RUN_SERIAL TRUE)

add_executable(perf_overlay_tests perf_overlay_tests.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/CalibrationCalc.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/CalibrationValidity.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/CalibrationMetrics.cpp)
target_include_directories(perf_overlay_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/lib/openvr/headers ${CMAKE_SOURCE_DIR}/lib/minhook/include)
target_link_libraries(perf_overlay_tests PRIVATE minhook shell32 ole32)
target_compile_definitions(perf_overlay_tests PRIVATE NOMINMAX UNICODE _CRT_SECURE_NO_WARNINGS)
set_target_properties(perf_overlay_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME perf_overlay COMMAND perf_overlay_tests)
set_tests_properties(perf_overlay PROPERTIES LABELS perf RUN_SERIAL TRUE)
