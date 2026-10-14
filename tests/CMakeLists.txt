link_libraries(spacecal_warnings)

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

add_executable(stick_profile_tests stick_profile_tests.cpp)
target_include_directories(stick_profile_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/lib/openvr/headers)
target_compile_definitions(stick_profile_tests PRIVATE NOMINMAX)
set_target_properties(stick_profile_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME stick_profile COMMAND stick_profile_tests)

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

add_executable(profile_tests profile_tests.cpp)
target_include_directories(profile_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/lib/openvr/headers)
target_compile_definitions(profile_tests PRIVATE NOMINMAX UNICODE _CRT_SECURE_NO_WARNINGS)
set_target_properties(profile_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME profile COMMAND profile_tests)

set(PERF_OVERLAY_UI_SOURCES
	${CMAKE_SOURCE_DIR}/lib/imgui/imgui.cpp
	${CMAKE_SOURCE_DIR}/lib/imgui/imgui_demo.cpp
	${CMAKE_SOURCE_DIR}/lib/imgui/imgui_draw.cpp
	${CMAKE_SOURCE_DIR}/lib/imgui/imgui_tables.cpp
	${CMAKE_SOURCE_DIR}/lib/imgui/imgui_widgets.cpp
	${CMAKE_SOURCE_DIR}/lib/implot/implot.cpp
	${CMAKE_SOURCE_DIR}/lib/implot/implot_items.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/MainWindow.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/Sidebar.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/Style.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/Widgets.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/UpdateUI.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/DriverConflictUI.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/pages/AboutPage.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/pages/BaseStationsPage.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/pages/CalibrationPage.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/pages/DevicePickers.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/pages/LearnPage.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/pages/SettingsPage.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/ui/pages/SmoothingPage.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/imgui_extensions.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/EmbeddedFontText.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/EmbeddedFontIcons.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/VRState.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/CalibrationDebug.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/CalibrationMetrics.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/CalibrationCalc.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/CalibrationValidity.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/AutoDetectController.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/DriverConflictState.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/LegacyInstall.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/Updater.cpp)
add_executable(perf_overlay_ui_tests perf_overlay_ui_tests.cpp ${PERF_OVERLAY_UI_SOURCES})
target_include_directories(perf_overlay_ui_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/lib/imgui ${CMAKE_SOURCE_DIR}/lib/openvr/headers ${CMAKE_SOURCE_DIR}/lib/minhook/include)
target_link_libraries(perf_overlay_ui_tests PRIVATE minhook shell32 ole32 winhttp)
target_compile_definitions(perf_overlay_ui_tests PRIVATE NOMINMAX UNICODE _CRT_SECURE_NO_WARNINGS IMGUI_DEFINE_MATH_OPERATORS IMGUI_ENABLE_TEST_ENGINE VR_API_EXPORT)
if(MSVC)
	target_compile_options(perf_overlay_ui_tests PRIVATE /bigobj)
else()
	target_compile_options(perf_overlay_ui_tests PRIVATE -Wa,-mbig-obj)
endif()
set_target_properties(perf_overlay_ui_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME perf_overlay_ui COMMAND perf_overlay_ui_tests)
set_tests_properties(perf_overlay_ui PROPERTIES LABELS perf RUN_SERIAL TRUE)

add_executable(scripts_tests scripts_tests.cpp)
target_include_directories(scripts_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib)
set_target_properties(scripts_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME scripts COMMAND scripts_tests)

add_executable(driver_race_tests driver_race_tests.cpp ${PERF_DRIVER_SOURCES})
target_include_directories(driver_race_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/driver ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib/openvr/headers ${CMAKE_SOURCE_DIR}/lib/minhook/include ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(driver_race_tests PRIVATE minhook)
target_compile_definitions(driver_race_tests PRIVATE NOMINMAX UNICODE _CRT_SECURE_NO_WARNINGS)
set_target_properties(driver_race_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME driver_race COMMAND driver_race_tests)
set_tests_properties(driver_race PROPERTIES LABELS perf RUN_SERIAL TRUE)

add_executable(protocol_tests protocol_tests.cpp protocol_tests_driverside.cpp)
target_include_directories(protocol_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib/openvr/headers)
target_compile_definitions(protocol_tests PRIVATE NOMINMAX)
set_target_properties(protocol_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME protocol COMMAND protocol_tests)

add_executable(math_tests math_tests.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/CalibrationCalc.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/CalibrationValidity.cpp
	${CMAKE_SOURCE_DIR}/src/overlay/CalibrationMetrics.cpp)
target_include_directories(math_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/src/driver ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/lib/openvr/headers)
target_link_libraries(math_tests PRIVATE shell32 ole32)
target_compile_definitions(math_tests PRIVATE NOMINMAX UNICODE _CRT_SECURE_NO_WARNINGS)
set_target_properties(math_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME math COMMAND math_tests)

add_executable(perf_overlay_config_tests perf_overlay_config_tests.cpp)
target_include_directories(perf_overlay_config_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/lib/openvr/headers ${CMAKE_SOURCE_DIR}/lib/minhook/include)
target_link_libraries(perf_overlay_config_tests PRIVATE minhook)
target_compile_definitions(perf_overlay_config_tests PRIVATE NOMINMAX UNICODE _CRT_SECURE_NO_WARNINGS)
set_target_properties(perf_overlay_config_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME perf_overlay_config COMMAND perf_overlay_config_tests)
set_tests_properties(perf_overlay_config PROPERTIES LABELS perf RUN_SERIAL TRUE)

add_executable(blend_tests blend_tests.cpp ${PERF_DRIVER_SOURCES})
target_include_directories(blend_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/driver ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib/openvr/headers ${CMAKE_SOURCE_DIR}/lib/minhook/include ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(blend_tests PRIVATE minhook)
target_compile_definitions(blend_tests PRIVATE NOMINMAX UNICODE _CRT_SECURE_NO_WARNINGS)
set_target_properties(blend_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME blend COMMAND blend_tests)

add_executable(utf8_acp_tests utf8_acp_tests.cpp ${CMAKE_SOURCE_DIR}/src/overlay/spacecal.manifest)
target_compile_definitions(utf8_acp_tests PRIVATE NOMINMAX UNICODE)
set_target_properties(utf8_acp_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME utf8_acp COMMAND utf8_acp_tests)
add_test(NAME utf8_acp_overlay COMMAND utf8_acp_tests $<TARGET_FILE:SpaceCalibratorOverlay>)
set_tests_properties(utf8_acp_overlay PROPERTIES SKIP_RETURN_CODE 77)

add_executable(keyboard_text_tests keyboard_text_tests.cpp)
target_include_directories(keyboard_text_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/overlay ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(keyboard_text_tests PRIVATE imgui)
set_target_properties(keyboard_text_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON FOLDER "tests")
add_test(NAME keyboard_text COMMAND keyboard_text_tests)
