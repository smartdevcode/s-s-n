add_library(net STATIC
    net.cpp
    server.cpp
)
add_library(taosim::net ALIAS net)

target_include_directories(net
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${src_dir}/decimal"
        "${src_dir}/serialization"
        "${src_dir}/util"
)

target_link_libraries(net
    PRIVATE
        Boost::boost
)
