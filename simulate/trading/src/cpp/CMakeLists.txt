add_subdirectory(accounting)
add_subdirectory(agent)
add_subdirectory(book)
add_subdirectory(base)
add_subdirectory(decimal)
add_subdirectory(exchange)
add_subdirectory(message)
add_subdirectory(mp)
add_subdirectory(net)
add_subdirectory(process)
add_subdirectory(serialization)
add_subdirectory(simulation)
add_subdirectory(stats)
add_subdirectory(util)

#-------------------------------------------------------------------------

add_library(simulator STATIC Simulation.cpp)
add_library(taosim::simulator ALIAS simulator)

target_include_directories(simulator
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        accounting
        agent
        book
        base
        decimal
        exchange
        message
        mp
        net
        process
        serialization
        stats
        util
)

target_link_libraries(simulator
    PUBLIC
        taosim::accounting
        taosim::agent
        taosim::book
        taosim::base
        taosim::decimal
        taosim::exchange
        taosim::message
        taosim::mp
        taosim::net
        taosim::serialization
        taosim::simulation
        taosim::stats
        taosim::util
        Boost::boost
        CLI11::CLI11
        bdl
        date::date date::date-tz
        fmt::fmt
        pugixml::static pugixml::pugixml
        pybind11::embed
)

#-------------------------------------------------------------------------

add_executable(${PROJECT_NAME}
    TheSimulatorModule.cpp
    main.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        $<TARGET_PROPERTY:taosim::simulator,INCLUDE_DIRECTORIES>
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        taosim::simulator
)

#-------------------------------------------------------------------------
