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
add_subdirectory(stats)
add_subdirectory(util)

#-------------------------------------------------------------------------

add_library(simulation STATIC
    SimulationConfig.cpp
    Simulation.cpp
    SimulationSignals.hpp
    SimulationState.hpp
    TimeConfig.cpp
)
add_library(taosim::simulation ALIAS simulation)

target_compile_options(simulation PRIVATE ${internal_compile_options})
target_compile_definitions(simulation PRIVATE ${internal_compile_definitions})
target_link_options(simulation PRIVATE ${internal_link_options})

target_include_directories(simulation
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

target_link_libraries(simulation
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

target_compile_options(${PROJECT_NAME} PRIVATE ${internal_compile_options})
target_compile_definitions(${PROJECT_NAME} PRIVATE ${internal_compile_definitions})
target_link_options(${PROJECT_NAME} PRIVATE ${internal_link_options})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        $<TARGET_PROPERTY:taosim::simulation,INCLUDE_DIRECTORIES>
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        taosim::simulation
)

#-------------------------------------------------------------------------
