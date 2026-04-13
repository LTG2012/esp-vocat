if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/touch_sensor_fsm.c")
set(sources "touch_sensor_fsm.c")
else()
set(sources "")
endif()

idf_component_register( SRCS "${sources}"
                        INCLUDE_DIRS "include")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/touch_sensor_fsm.c")
    add_prebuilt_library(touch_sensor_fsm_lib "${CMAKE_CURRENT_SOURCE_DIR}/${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}/${CONFIG_IDF_TARGET}/libtouch_sensor_fsm.a")
    target_include_directories(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(${COMPONENT_LIB} INTERFACE touch_sensor_fsm_lib)
else()
    include(package_manager)
    cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wignored-qualifiers)
endif()
