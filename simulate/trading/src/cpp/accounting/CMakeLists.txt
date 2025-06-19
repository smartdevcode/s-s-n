add_library(accounting STATIC
    Account.cpp
    AccountRegistry.cpp
    Balance.cpp
    Balances.cpp
    BalanceLogger.cpp
    Balances.cpp
    Collateral.cpp
    FreeInfo.cpp
    Loan.cpp
    common.cpp
    margin_utils.cpp
)
add_library(taosim::accounting ALIAS accounting)

target_include_directories(accounting
    PUBLIC
        include
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${src_dir}/decimal"
        "${src_dir}/exchange"
        "${src_dir}/mp"
        "${src_dir}/serialization"
        "${src_dir}/util"
)

target_link_libraries(accounting
    PUBLIC
        Boost::boost
)
