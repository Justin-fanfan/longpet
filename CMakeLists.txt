cmake_minimum_required(VERSION 3.21)

project(
    LongPet
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(
    Qt6 6.5
    REQUIRED
    COMPONENTS
        Core
        Gui
        Widgets
        Svg
)

set(
    LONGPET_UI_SOURCES
    src/mainwindow.cpp
    src/mainwindow.h
    src/pages/CompanionPage.cpp
    src/pages/CompanionPage.h
    src/pages/ConversationPage.cpp
    src/pages/ConversationPage.h
    src/pages/CarePage.cpp
    src/pages/CarePage.h
    src/pages/ReminderPage.cpp
    src/pages/ReminderPage.h
    src/pages/ReminderEditPage.cpp
    src/pages/ReminderEditPage.h
    src/pages/SettingsPage.cpp
    src/pages/SettingsPage.h
    src/pages/EmergencyPage.cpp
    src/pages/EmergencyPage.h
    src/pages/SleepPage.cpp
    src/pages/SleepPage.h
    src/pages/HomePage.cpp
    src/pages/HomePage.h
    src/widgets/PetFaceWidget.cpp
    src/widgets/PetFaceWidget.h
    src/widgets/VisualComponents.cpp
    src/widgets/VisualComponents.h
    src/widgets/VisualTokens.h
)

qt_add_executable(
    LongPet
    src/main.cpp
    ${LONGPET_UI_SOURCES}
    resources/resources.qrc
)

target_link_libraries(
    LongPet PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
)

target_include_directories(
    LongPet
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(
    LongPet
    PRIVATE
        LONGPET_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(
        LongPet
        PRIVATE
            /W4
            /permissive-
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

set_target_properties(LongPet PROPERTIES WIN32_EXECUTABLE TRUE)

install(
    TARGETS LongPet
    RUNTIME DESTINATION bin
)

option(LONGPET_BUILD_TESTS "Build LongPet V0.1 UI tests" OFF)

if(LONGPET_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    qt_add_executable(
        LongPetV01Tests
        tests/V01UiTest.cpp
        ${LONGPET_UI_SOURCES}
        resources/resources.qrc
    )

    target_include_directories(
        LongPetV01Tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(
        LongPetV01Tests
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::Svg
            Qt6::Test
    )

    if(MSVC)
        target_compile_options(LongPetV01Tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(LongPetV01Tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME LongPet.V01.Ui COMMAND LongPetV01Tests)
    set_tests_properties(
        LongPet.V01.Ui
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
