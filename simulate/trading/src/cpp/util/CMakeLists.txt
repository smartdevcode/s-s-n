add_library(util STATIC
    Cancellation.cpp
    EventRecord.cpp
    GBMValuationModel.hpp
    LimitedDeque.hpp
    Order.cpp
    OrderContainer.hpp
    OrderFactory.cpp
    ParameterStorage.cpp
    SubscriptionRegistry.hpp
    TickContainer.cpp
    Timestamp.hpp
    Trade.cpp
    TradeFactory.cpp
    common.hpp
    util.cpp
    Flags.hpp
)
add_library(taosim::util ALIAS util)

target_compile_options(util PRIVATE ${internal_compile_options})
target_compile_definitions(util PRIVATE ${internal_compile_definitions})
target_link_options(util PRIVATE ${internal_link_options})

target_include_directories(util
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${src_dir}/accounting"
        "${src_dir}/agent"
        "${src_dir}/base"
        "${src_dir}/book"
        "${src_dir}/decimal"
        "${src_dir}/exchange"
        "${src_dir}/message"
        "${src_dir}/mp"
        "${src_dir}/serialization"
)

target_link_libraries(util
    PUBLIC
        Boost::boost
        fmt::fmt
)
