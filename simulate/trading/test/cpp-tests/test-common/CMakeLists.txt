add_library(common STATIC
    formatting.hpp
)
add_library(taosim::test::common ALIAS common)

target_include_directories(common
    PRIVATE
        "${src_dir}/decimal"
)

target_link_libraries(common
    PRIVATE
        taosim::decimal
)
