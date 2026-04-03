cmake_minimum_required(VERSION 3.16)
project(StressTestingEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_TESTS "Build unit tests" ON)

# Find threads
find_package(Threads REQUIRED)

# Source files
set(ENGINE_SOURCES
    src/core/black_scholes.cpp
    src/core/monte_carlo.cpp
    src/core/portfolio.cpp
    src/core/market_data.cpp
    src/strategies/options_strategies.cpp
    src/strategies/strategy_manager.cpp
    src/regime/hidden_markov_model.cpp
    src/regime/regime_detector.cpp
    src/stress/stress_engine.cpp
    src/stress/scenario_generator.cpp
    src/stress/historical_scenarios.cpp
    src/visualization/web_server.cpp
    src/visualization/data_broadcaster.cpp
    src/utils/math_utils.cpp
    src/utils/csv_parser.cpp
    src/utils/json_writer.cpp
    src/core/arima.cpp
    src/core/backtester.cpp
    src/core/statistical_tests.cpp
)

# Main library
add_library(stress_engine_lib STATIC ${ENGINE_SOURCES})
target_include_directories(stress_engine_lib PUBLIC include)
target_link_libraries(stress_engine_lib PUBLIC Threads::Threads)

# Main executable
add_executable(stress_engine src/main.cpp)
target_link_libraries(stress_engine PRIVATE stress_engine_lib)

# Tests
if(BUILD_TESTS)
    add_executable(run_tests tests/test_main.cpp)
    target_link_libraries(run_tests PRIVATE stress_engine_lib)
endif()

# Install web assets alongside binary
install(DIRECTORY web/ DESTINATION share/stress_engine/web)
install(TARGETS stress_engine DESTINATION bin)
