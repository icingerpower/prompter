cmake_minimum_required(VERSION 3.5)

project(PrompterTests LANGUAGES CXX)

enable_testing()

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

setup_qcoro(COMPONENTS Core)

add_executable(Test_Prompter_Placeholder test_prompter.cpp)
add_test(NAME Test_Prompter_Placeholder COMMAND Test_Prompter_Placeholder)
target_link_libraries(Test_Prompter_Placeholder PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    -Wl,--whole-archive PrompterLib -Wl,--no-whole-archive)
target_include_directories(Test_Prompter_Placeholder PRIVATE ../PrompterLib)

add_executable(Test_CLI test_cli.cpp)
add_test(NAME Test_CLI COMMAND Test_CLI)
target_link_libraries(Test_CLI PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    QCoro::Core
    -Wl,--whole-archive PrompterLib -Wl,--no-whole-archive)
target_include_directories(Test_CLI PRIVATE ../PrompterLib ../../common)

add_executable(Test_ConfigManager test_config_manager.cpp)
add_test(NAME Test_ConfigManager COMMAND Test_ConfigManager)
target_link_libraries(Test_ConfigManager PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    -Wl,--whole-archive PrompterLib -Wl,--no-whole-archive)
target_include_directories(Test_ConfigManager PRIVATE ../PrompterLib ../../common)
