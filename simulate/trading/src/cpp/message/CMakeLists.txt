file(GLOB message_sources CONFIGURE_DEPENDS "*.cpp")
add_library(message STATIC ${message_sources})
add_library(taosim::message ALIAS message)

target_include_directories(message
    PUBLIC
        include
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${src_dir}/accounting"
        "${src_dir}/base"
        "${src_dir}/serialization"
        "${src_dir}/util"
)

target_link_libraries(message
    PRIVATE
        taosim::accounting
        taosim::book
        taosim::util
)
