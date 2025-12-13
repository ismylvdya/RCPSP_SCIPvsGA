cmake_minimum_required(VERSION 3.31)
project(RCPSP)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Пути к SCIP
set(SCIP_ROOT "/opt/homebrew/opt/scip")
include_directories(${SCIP_ROOT}/include)
link_directories(${SCIP_ROOT}/lib)

# Qt6
find_package(Qt6 COMPONENTS Widgets REQUIRED)
set(CMAKE_AUTOMOC ON)

# Исходники
set(SOURCES
        main_scip.cpp
        rcpsp_parser.cpp
)

add_executable(RCPSP ${SOURCES})

# Линкуем SCIP
target_link_libraries(RCPSP scip)
# Линкуем Qt
target_link_libraries(RCPSP Qt6::Widgets)
