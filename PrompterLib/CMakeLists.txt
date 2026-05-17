cmake_minimum_required(VERSION 3.14)

project(PrompterLib LANGUAGES CXX)

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)

setup_qcoro(COMPONENTS Core)

include(../../common/aicli/aicli.cmake)

add_library(PrompterLib STATIC
    PrompterLib.h
    PrompterLib.cpp
    ConfigManager.h
    ConfigManager.cpp
    PromptEngineer.h
    PromptEngineer.cpp
    ${AICLI_FILES}
)

target_link_libraries(PrompterLib
    PUBLIC
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    QCoro::Core
)

target_include_directories(PrompterLib PUBLIC ../../common ${CMAKE_CURRENT_SOURCE_DIR})
