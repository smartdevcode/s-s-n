add_library(util STATIC
    Cancellation.cpp
    ClosePosition.cpp
    GBMValuationModel.hpp
    Order.cpp
    OrderFactory.cpp
    SubscriptionRegistry.hpp
    Timestamp.hpp
    Trade.cpp
    TradeFactory.cpp
    common.hpp
    util.cpp
    Flags.hpp
    InstructionLogger.cpp
)
add_library(taosim::util ALIAS util)

target_include_directories(util
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${src_dir}/accounting"
        "${src_dir}/agent"
        "${src_dir}/base"
        "${src_dir}/exchange"
        "${src_dir}/mp"
        "${src_dir}/serialization"
        "${src_dir}/simulation/include"
)

target_link_libraries(util
    PUBLIC
        taosim::book
        taosim::decimal
        taosim::event
        taosim::message
        Boost::boost
        fmt::fmt
        taosim::serialization
)
