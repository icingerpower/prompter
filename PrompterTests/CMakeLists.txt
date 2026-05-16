cmake_minimum_required(VERSION 3.5)

project(PrompterTests LANGUAGES CXX)

enable_testing()

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

add_executable(Test_Prompter_Placeholder test_prompter.cpp)
add_test(NAME Test_Prompter_Placeholder COMMAND Test_Prompter_Placeholder)
target_link_libraries(Test_Prompter_Placeholder PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    -Wl,--whole-archive PrompterLib -Wl,--no-whole-archive)
target_include_directories(Test_Prompter_Placeholder PRIVATE ../PrompterLib)
