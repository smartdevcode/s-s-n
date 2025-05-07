add_library(base STATIC
    IConfigurable.hpp
    CSVPrintable.hpp
    IHumanPrintable.hpp
    IMessageable.cpp
    IPrintable.hpp
)
add_library(taosim::base ALIAS base)

target_compile_options(base PRIVATE ${internal_compile_options})
target_compile_definitions(base PRIVATE ${internal_compile_definitions})
target_link_options(base PRIVATE ${internal_link_options})

target_include_directories(base
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${src_dir}"
        "${src_dir}/accounting"
        "${src_dir}/agent"
        "${src_dir}/book"
        "${src_dir}/decimal"
        "${src_dir}/exchange"
        "${src_dir}/message"
        "${src_dir}/mp"
        "${src_dir}/net"
        "${src_dir}/process"
        "${src_dir}/serialization"
        "${src_dir}/util"
)

target_link_libraries(base
    PRIVATE
        taosim::exchange
        pugixml::static pugixml::pugixml
        pybind11::embed
    INTERFACE
        taosim::mp
)
