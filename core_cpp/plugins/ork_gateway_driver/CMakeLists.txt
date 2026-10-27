set(TARGET_NAME OrkGatewayDriver)

set(GATEWAY_HDRS
    include/ourokore/gateway/gateway_export.h
    include/ourokore/gateway/gateway_api.h
    include/ourokore/gateway/IGatewayDriver.hpp
    src/GatewayDriver.hpp
)

set(GATEWAY_SRCS
    src/GatewayDriver.cpp
    src/gateway_api.cpp
)

add_library(${TARGET_NAME} MODULE ${GATEWAY_HDRS} ${GATEWAY_SRCS})

target_compile_definitions(${TARGET_NAME} PRIVATE OUROKORE_GATEWAY_EXPORTS)

target_include_directories(${TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# 僅鏈結基礎通用工具庫 (ourokore_base)，與核心引擎完全解耦
target_link_libraries(${TARGET_NAME} PRIVATE ourokore_base)

# 本地編譯輸出路徑：自動放置於 bin/plugins/
set_target_properties(${TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins"
    PREFIX "" # 各平台統一為 OrkGatewayDriver.dll / OrkGatewayDriver.so
)

# 安裝外掛動態庫至 bin/plugins/
install(TARGETS ${TARGET_NAME}
    RUNTIME DESTINATION bin/plugins
    LIBRARY DESTINATION bin/plugins
)

if (MSVC)
    install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}> DESTINATION bin/plugins OPTIONAL)
endif()

install(DIRECTORY include/ DESTINATION include)
