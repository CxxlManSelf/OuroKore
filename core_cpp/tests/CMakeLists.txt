project(ourokore_tests LANGUAGES CXX)

include_directories("../include")

add_executable(basic_tests basic_tests.cpp)
target_link_libraries(basic_tests ourokore_core)
