project(ourokore_base LANGUAGES CXX)

file(GLOB_RECURSE BASE_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(ourokore_base STATIC ${BASE_SRCS})
target_include_directories(ourokore_base PUBLIC "${CMAKE_SOURCE_DIR}/include")

if(UNIX)
    target_link_libraries(ourokore_base PUBLIC pthread)
endif()

install(TARGETS ourokore_base
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)
