cmake_minimum_required(VERSION 3.21)
project(APDUTracerNative VERSION 2.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(APDU_BUILD_GUI "Build the Qt Widgets application" ON)
include(CTest)

function(apdu_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Windows 10 (_WIN32_WINNT_WIN10) is the minimum supported version. Keep the
# fallback in src/capture/UsbPcapBackend.cpp equal to this value.
function(apdu_windows_target target)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
                               _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endfunction()

add_library(apdu_core src/core/TraceDecoder.cpp src/core/CcidChannel.cpp src/core/T1Decoder.cpp)
target_include_directories(apdu_core PUBLIC src)
apdu_warnings(apdu_core)

add_library(apdu_capture src/capture/UsbPcapBackend.cpp)
target_include_directories(apdu_capture PUBLIC src)
if(WIN32)
    apdu_windows_target(apdu_capture)
    # SetupAPI and the configuration manager name the devices on each root hub.
    target_link_libraries(apdu_capture PRIVATE setupapi cfgmgr32)
endif()
apdu_warnings(apdu_capture)

if(BUILD_TESTING)
    add_executable(decoder_tests tests/decoder_tests.cpp)
    target_link_libraries(decoder_tests PRIVATE apdu_core)
    apdu_warnings(decoder_tests)
    add_test(NAME decoder_tests COMMAND decoder_tests)
    add_executable(t1_tests tests/t1_tests.cpp)
    target_link_libraries(t1_tests PRIVATE apdu_core)
    apdu_warnings(t1_tests)
    add_test(NAME t1_tests COMMAND t1_tests)
    add_executable(transport_tests tests/transport_tests.cpp)
    target_link_libraries(transport_tests PRIVATE apdu_core)
    apdu_warnings(transport_tests)
    add_test(NAME transport_tests COMMAND transport_tests)
    if(WIN32)
        # This standalone harness replaces driver-facing Win32 calls; it never
        # opens a real capture device and includes the implementation itself.
        add_executable(usbpcap_backend_tests tests/UsbPcapBackendTests.cpp)
        apdu_windows_target(usbpcap_backend_tests)
        target_link_libraries(usbpcap_backend_tests PRIVATE setupapi cfgmgr32)
        apdu_warnings(usbpcap_backend_tests)
        add_test(NAME usbpcap_backend_tests COMMAND usbpcap_backend_tests)
        set_tests_properties(usbpcap_backend_tests PROPERTIES TIMEOUT 15)
    endif()
endif()

if(APDU_BUILD_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()
    add_library(apdu_ui
        src/ui/RawCaptureWriter.cpp src/ui/RawCaptureWriter.h
        src/ui/TraceView.cpp src/ui/TraceView.h
        src/ui/TraceWorker.cpp src/ui/TraceWorker.h
        src/ui/MainWindow.cpp src/ui/MainWindow.h
    )
    target_link_libraries(apdu_ui PUBLIC Qt6::Widgets apdu_core apdu_capture)
    apdu_warnings(apdu_ui)
    qt_add_executable(APDUTracer WIN32 src/main.cpp)
    target_link_libraries(APDUTracer PRIVATE apdu_ui)
    # Logo renders from assets/generate_icons.py: PNGs for the window icon
    # (no Qt SVG module needed) and an .ico embedded in the executable.
    set(apdu_icon_sizes 16 24 32 48 64 128 256)
    list(TRANSFORM apdu_icon_sizes PREPEND "assets/icons/apdu-tracer-" OUTPUT_VARIABLE apdu_icon_files)
    list(TRANSFORM apdu_icon_files APPEND ".png")
    qt_add_resources(APDUTracer "app_icons" PREFIX "/" BASE assets FILES ${apdu_icon_files})
    if(WIN32)
        target_sources(APDUTracer PRIVATE assets/apdu-tracer.rc)
    endif()
    # project() VERSION is the single source of the application version.
    target_compile_definitions(APDUTracer PRIVATE APDU_TRACER_VERSION="${PROJECT_VERSION}")
    apdu_warnings(APDUTracer)

    if(BUILD_TESTING)
        add_executable(qt_smoke_tests ui-tests/qt_smoke_tests.cpp)
        target_link_libraries(qt_smoke_tests PRIVATE apdu_ui)
        apdu_warnings(qt_smoke_tests)
        add_test(NAME generate_fixture COMMAND decoder_tests --write-fixture "${CMAKE_CURRENT_BINARY_DIR}/smoke.pcap")
        set_tests_properties(generate_fixture PROPERTIES FIXTURES_SETUP capture_fixture)
        add_test(NAME qt_smoke_tests COMMAND qt_smoke_tests "${CMAKE_CURRENT_BINARY_DIR}/smoke.pcap")
        set_tests_properties(qt_smoke_tests PROPERTIES
            FIXTURES_REQUIRED capture_fixture ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
        add_executable(worker_tests ui-tests/worker_tests.cpp)
        target_link_libraries(worker_tests PRIVATE apdu_ui)
        apdu_warnings(worker_tests)
        add_test(NAME worker_tests COMMAND worker_tests)
        set_tests_properties(worker_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    endif()

    install(TARGETS APDUTracer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # The redistributed Qt runtime requires its license texts and notices.
    install(FILES LICENSE README.md THIRD-PARTY-NOTICES.md DESTINATION .)
    install(DIRECTORY licenses DESTINATION .)
    if(WIN32)
        # Qt Widgets renders with the raster engine, so the 20 MB software
        # OpenGL fallback (opengl32sw.dll) is never loaded.
        qt_generate_deploy_app_script(TARGET APDUTracer OUTPUT_SCRIPT deploy_script
            DEPLOY_TOOL_OPTIONS --no-opengl-sw)
        install(SCRIPT ${deploy_script})
    endif()
endif()
