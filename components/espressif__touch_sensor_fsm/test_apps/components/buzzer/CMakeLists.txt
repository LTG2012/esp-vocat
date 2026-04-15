if(NOT ${IDF_TARGET} STREQUAL "linux")
set(requires driver)
if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
list(APPEND requires "esp_driver_ledc")
endif()
else()
set(requires "")
endif()

idf_component_register(SRCS "buzzer.c"
                        INCLUDE_DIRS "."
                        REQUIRES ${requires})
