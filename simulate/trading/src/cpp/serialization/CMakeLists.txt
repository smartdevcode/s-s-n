add_library(serialization STATIC
    CheckpointSerializable.hpp
    JsonSerializable.hpp
    Recoverable.hpp
    json_util.cpp
)
add_library(taosim::serialization ALIAS serialization)

target_include_directories(serialization
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${src_dir}/decimal"
        "${src_dir}/mp"
)

target_link_libraries(serialization PUBLIC taosim::decimal)
