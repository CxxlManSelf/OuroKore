string(TIMESTAMP BUILD_TIMESTAMP "%y%j%H%M")
set(PROJECT_VERSION_PATCH ${BUILD_TIMESTAMP})
project(ourokore_base VERSION 0.1.${PROJECT_VERSION_PATCH} DESCRIPTION "OuroKore's base layer" LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()
set(TARGET_NAME ${PROJECT_NAME})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../version.rc.in 
                       ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}/version.rc 
                      @ONLY)


file(GLOB_RECURSE BASE_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

if(UNIX)
    set(LIBSRC_LIST ${LIBSRC_LIST} pthread)
endif()

add_library(${TARGET_NAME} SHARED ${BASE_SRCS})
set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_compile_definitions(${TARGET_NAME} PRIVATE OUROKORE_BASE_EXPORTS)
target_sources(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}/version.rc)
target_include_directories(${TARGET_NAME} PUBLIC "${CMAKE_SOURCE_DIR}/include")
if (NOT LIBSRC_LIST STREQUAL "")
    target_link_libraries(${TARGET_NAME} ${LIBSRC_LIST})
endif()

install(TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )

if (MSVC)        
    install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}> DESTINATION bin OPTIONAL)
endif()

