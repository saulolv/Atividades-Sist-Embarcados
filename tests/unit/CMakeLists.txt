cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(radar_unit_tests)

# Add include path for common.h
target_include_directories(app PRIVATE ../../src)

target_sources(app PRIVATE ../../src/utils.c test_logic.c test_fsm.c)
