cmake_minimum_required(VERSION 3.16)
project(imgui-color-text-edit CXX)

set(CMAKE_DEBUG_POSTFIX d)

find_package(imgui CONFIG REQUIRED)

add_library(${PROJECT_NAME} STATIC TextEditor.cpp)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        $<INSTALL_INTERFACE:include>
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_link_libraries(${PROJECT_NAME} PUBLIC imgui::imgui)

install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}_target
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

if(NOT ICTE_SKIP_HEADERS)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/TextEditor.h DESTINATION include)
endif()

include(CMakePackageConfigHelpers)
configure_package_config_file(imgui-color-text-edit-config.cmake.in
    imgui-color-text-edit-config.cmake
    INSTALL_DESTINATION share/imgui-color-text-edit)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/imgui-color-text-edit-config.cmake
    DESTINATION share/imgui-color-text-edit)

install(
    EXPORT ${PROJECT_NAME}_target
    NAMESPACE ${PROJECT_NAME}::
    FILE ${PROJECT_NAME}-targets.cmake
    DESTINATION share/${PROJECT_NAME}
)
