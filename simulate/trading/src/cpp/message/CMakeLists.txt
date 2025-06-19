add_library(message STATIC
    ExchangeAgentMessagePayloads.cpp
    Message.cpp
    MessagePayload.cpp
    MessageQueue.cpp
    MessageType.hpp
    MultiBookMessagePayloads.cpp
    PayloadFactory.cpp
)
add_library(taosim::message ALIAS message)

target_include_directories(message
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${src_dir}/accounting"
        "${src_dir}/base"
        "${src_dir}/book"
        "${src_dir}/serialization"
        "${src_dir}/util"
)

target_link_libraries(message
    PRIVATE
        taosim::accounting
        taosim::util
)
