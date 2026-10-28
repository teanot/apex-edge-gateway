add_executable(apex_tests
    test_main.cpp
    test_ring_buffer.cpp
    test_anomaly_detector.cpp
    test_persistence.cpp
)

target_include_directories(apex_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(apex_tests PRIVATE apex_core)

add_test(NAME ApexGatewayUnitTests COMMAND apex_tests)
