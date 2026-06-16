add_executable(test_core_types
    test_core_types.cpp
)

add_executable(test_limit_order_book
    test_limit_order_book.cpp
)

add_executable(test_pooled_limit_order_book
    test_pooled_limit_order_book.cpp
)

add_executable(test_spsc_ring_buffer
    test_spsc_ring_buffer.cpp
)

add_executable(test_anomaly_detection
    test_anomaly_detection.cpp
)

add_executable(test_csv_parser
    test_csv_parser.cpp
)

add_executable(test_replay_engine
    test_replay_engine.cpp
)

target_link_libraries(test_core_types
    PRIVATE
        datagine::datagine
)

target_link_libraries(test_limit_order_book
    PRIVATE
        datagine::datagine
)

target_link_libraries(test_pooled_limit_order_book
    PRIVATE
        datagine::datagine
)

target_link_libraries(test_spsc_ring_buffer
    PRIVATE
        datagine::datagine
        Threads::Threads
)

target_link_libraries(test_anomaly_detection
    PRIVATE
        datagine::datagine
)

target_link_libraries(test_csv_parser
    PRIVATE
        datagine::datagine
)

target_link_libraries(test_replay_engine
    PRIVATE
        datagine::datagine
)

if(MSVC)
    target_compile_options(test_core_types PRIVATE /W4)
    target_compile_options(test_limit_order_book PRIVATE /W4)
    target_compile_options(test_pooled_limit_order_book PRIVATE /W4)
    target_compile_options(test_spsc_ring_buffer PRIVATE /W4)
    target_compile_options(test_anomaly_detection PRIVATE /W4)
    target_compile_options(test_csv_parser PRIVATE /W4)
    target_compile_options(test_replay_engine PRIVATE /W4)
else()
    target_compile_options(test_core_types PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_limit_order_book PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_pooled_limit_order_book PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_spsc_ring_buffer PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_anomaly_detection PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_csv_parser PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_replay_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME core_types COMMAND test_core_types)
add_test(NAME limit_order_book COMMAND test_limit_order_book)
add_test(NAME pooled_limit_order_book COMMAND test_pooled_limit_order_book)
add_test(NAME spsc_ring_buffer COMMAND test_spsc_ring_buffer)
add_test(NAME anomaly_detection COMMAND test_anomaly_detection)
add_test(NAME csv_parser COMMAND test_csv_parser)
add_test(NAME replay_engine COMMAND test_replay_engine)
