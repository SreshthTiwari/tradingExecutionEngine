cmake_minimum_required(VERSION 3.16)
project(TradingExecutionEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

add_library(engine
    src/core/order_book.cpp
    src/core/matching_engine.cpp
    src/core/risk_checks.cpp
    src/core/trade_execution.cpp
    src/core/command.cpp
    src/market_data/market_data_interface.cpp
    src/market_data/replay.cpp
    src/network/tcp_gateway.cpp
)
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC Threads::Threads)
target_compile_options(engine PRIVATE -Wall -Wextra)
target_compile_definitions(engine PRIVATE
    MARKET_DATA_SCRIPT="${CMAKE_SOURCE_DIR}/src/market_data/yahoo_data.py")

add_executable(trading_system src/main.cpp)
target_link_libraries(trading_system PRIVATE engine)

add_executable(replay tools/replay.cpp)
target_link_libraries(replay PRIVATE engine)

enable_testing()
foreach(name order_book_test matching_engine_test random_test command_test replay_test gateway_test market_data_test)
    add_executable(${name} tests/${name}.cpp)
    target_include_directories(${name} PRIVATE tests)
    target_link_libraries(${name} PRIVATE engine)
    add_test(NAME ${name} COMMAND ${name})
endforeach()
target_compile_definitions(replay_test PRIVATE SAMPLE_DATA_PATH="${CMAKE_SOURCE_DIR}/data/sample_orders.txt")

foreach(name latency_tests throughput_tests)
    add_executable(${name} benchmarks/${name}.cpp)
    target_include_directories(${name} PRIVATE benchmarks)
    target_link_libraries(${name} PRIVATE engine)
endforeach()
