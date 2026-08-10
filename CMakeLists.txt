cmake_minimum_required(VERSION 3.21)

project(
    LongPet
    VERSION 0.1.0
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(
    Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Svg
)

qt_add_executable(
    LongPet

    src/main.cpp
    src/MainWindow.h src/MainWindow.cpp
    resources/resources.qrc



)

target_link_libraries(
    LongPet PRIVATE

    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)

target_include_directories(LongPet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Warnings
if(MSVC)

    target_compile_options(
        LongPet
        PRIVATE
            /W4
    )

else()

    target_compile_options(
        LongPet
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

endif()

install(
    TARGETS LongPet
    RUNTIME DESTINATION bin
)