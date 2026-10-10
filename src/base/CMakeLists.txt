add_library(base ${CTS_LIBRARY_TYPE})

target_sources(
    base
    PRIVATE
        version.cc
    PUBLIC
        FILE_SET HEADERS
        FILES
            base_export.h
            version.h
)

if(CTS_COMPONENT_BUILD)
    target_compile_definitions(base PRIVATE BASE_IMPLEMENTATION)
endif()

target_link_libraries(base PUBLIC cts::config)

add_library(cts::base ALIAS base)
