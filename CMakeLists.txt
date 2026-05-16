cmake_minimum_required(VERSION 3.5)

project(Prompter)

enable_testing()

option(ENABLE_COVERAGE "Enable gcov/lcov code coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

add_subdirectory(PrompterLib)
add_subdirectory(Prompter)
add_subdirectory(PrompterTests)

add_dependencies(Prompter
    PrompterLib
)
add_dependencies(Test_Prompter_Placeholder
    PrompterLib
)
add_dependencies(Test_CLI
    PrompterLib
)
