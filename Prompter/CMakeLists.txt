cmake_minimum_required(VERSION 3.5)

project(Prompter VERSION 0.1 LANGUAGES CXX)

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

setup_qcoro(COMPONENTS Core)

include(../../common/workingdirectory/workingdirectory.cmake)

include(gui/gui.cmake)

set(PROJECT_SOURCES
    main.cpp
    ${WORKING_DIR_FILES}
    ${GUI_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Prompter
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(Prompter SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(Prompter
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_include_directories(Prompter PRIVATE ../../common)

target_link_libraries(Prompter PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    QCoro::Core
    -Wl,--whole-archive PrompterLib -Wl,--no-whole-archive)

set_target_properties(Prompter PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS Prompter
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Prompter)
endif()
