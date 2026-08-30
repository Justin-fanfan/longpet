cmake_minimum_required(VERSION 3.21)

project(
    LongPet
    VERSION 0.2.0
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
        Sql
        Network
        WebSockets
)

if(UNIX AND NOT APPLE)
    find_package(ALSA QUIET)
endif()

set(
    LONGPET_SOURCES
    src/app/Application.cpp
    src/app/Application.h
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/FamilyLinkController.cpp
    src/app/FamilyLinkController.h
    src/model/ReminderModels.h
    src/model/AiModels.cpp
    src/model/AiModels.h
    src/model/NetworkModels.h
    src/model/SettingsModels.h
    src/model/SystemModels.h
    src/model/VideoCallModels.h
    src/model/MediaFrameProtocol.cpp
    src/model/MediaFrameProtocol.h
    src/platform/NetworkStatusAdapter.cpp
    src/platform/NetworkStatusAdapter.h
    src/platform/NetworkManagerAdapter.cpp
    src/platform/NetworkManagerAdapter.h
    src/platform/ProviderHttpClient.cpp
    src/platform/ProviderHttpClient.h
    src/platform/OpenAiCompatibleProviders.cpp
    src/platform/OpenAiCompatibleProviders.h
    src/platform/AliyunProviders.cpp
    src/platform/AliyunProviders.h
    src/platform/AiProviderFactory.cpp
    src/platform/AiProviderFactory.h
    src/platform/VoiceAudioAdapter.cpp
    src/platform/VoiceAudioAdapter.h
    src/platform/AudioVolumeAdapter.cpp
    src/platform/AudioVolumeAdapter.h
    src/platform/BacklightAdapter.cpp
    src/platform/BacklightAdapter.h
    src/platform/PowerStatusAdapter.cpp
    src/platform/PowerStatusAdapter.h
    src/platform/FamilyLinkHttpAdapter.cpp
    src/platform/FamilyLinkHttpAdapter.h
    src/platform/CallPromptPlayerAdapter.cpp
    src/platform/CallPromptPlayerAdapter.h
    src/platform/VideoCallMediaAdapter.cpp
    src/platform/VideoCallMediaAdapter.h
    src/data/DatabaseManager.cpp
    src/data/DatabaseManager.h
    src/data/ReminderRepository.cpp
    src/data/ReminderRepository.h
    src/data/CareEventRepository.cpp
    src/data/CareEventRepository.h
    src/data/SettingsRepository.cpp
    src/data/SettingsRepository.h
    src/data/AiConfigRepository.cpp
    src/data/AiConfigRepository.h
    src/services/ReminderService.cpp
    src/services/ReminderService.h
    src/services/NetworkService.cpp
    src/services/NetworkService.h
    src/services/CareService.cpp
    src/services/CareService.h
    src/services/SettingsService.cpp
    src/services/SettingsService.h
    src/services/SystemService.cpp
    src/services/SystemService.h
    src/services/FamilyLinkService.cpp
    src/services/FamilyLinkService.h
    src/services/VideoCallService.cpp
    src/services/VideoCallService.h
    src/services/VideoCallPorts.h
    src/services/MediaSessionCoordinator.cpp
    src/services/MediaSessionCoordinator.h
    src/services/VoiceInteractionPorts.h
    src/services/VoiceInteractionService.cpp
    src/services/VoiceInteractionService.h
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
    src/pages/NetworkSetupPage.cpp
    src/pages/NetworkSetupPage.h
    src/pages/VideoCallPage.cpp
    src/pages/VideoCallPage.h
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

function(longpet_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Compile the application layers once. Both the product executable and the
# test executable reuse these objects, avoiding two concurrent full builds.
add_library(LongPetCore OBJECT ${LONGPET_SOURCES})

target_link_libraries(
    LongPetCore PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
        Qt6::Sql
        Qt6::Network
        Qt6::WebSockets
)

target_include_directories(
    LongPetCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(ALSA_FOUND)
    target_link_libraries(LongPetCore PUBLIC ALSA::ALSA)
    target_compile_definitions(LongPetCore PRIVATE LONGPET_HAS_ALSA=1)
endif()

longpet_enable_warnings(LongPetCore)

qt_add_executable(
    LongPet
    src/main.cpp
    resources/resources.qrc
)

target_link_libraries(LongPet PRIVATE LongPetCore)

target_compile_definitions(
    LongPet
    PRIVATE
        LONGPET_VERSION="${PROJECT_VERSION}"
)

longpet_enable_warnings(LongPet)

set_target_properties(LongPet PROPERTIES WIN32_EXECUTABLE TRUE)

install(
    TARGETS LongPet
    RUNTIME DESTINATION bin
)

option(LONGPET_BUILD_TESTS "Build LongPet V0.2 tests" OFF)

if(LONGPET_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    qt_add_executable(
        LongPetV02Tests
        tests/V02Test.cpp
        resources/resources.qrc
    )

    target_link_libraries(
        LongPetV02Tests
        PRIVATE
            LongPetCore
            Qt6::Test
    )

    longpet_enable_warnings(LongPetV02Tests)

    add_test(NAME LongPet.V02 COMMAND LongPetV02Tests)
    set_tests_properties(
        LongPet.V02
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
