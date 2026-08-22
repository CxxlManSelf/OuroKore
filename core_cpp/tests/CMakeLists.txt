project(ourokore_tests LANGUAGES CXX)

include_directories("../include")

add_executable(basic_tests basic_tests.cpp)
target_link_libraries(basic_tests ourokore_core)

add_executable(serialization_tests serialization_tests.cpp)
target_link_libraries(serialization_tests ourokore_core)

