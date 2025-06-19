/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "taosim/simulation/SimulationManager.hpp"
#include "common.hpp"

#include <CLI/CLI.hpp>
#include <pybind11/embed.h>
namespace py = pybind11;

//-------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    CLI::App app{"ExchangeSimulator v2.0"};

    py::scoped_interpreter guard{};

    CLI::Option_group* initGroup = app.add_option_group("Init");
    fs::path config;
    initGroup->add_option("-f,--config-file", config, "Simulation config file")
        ->check(CLI::ExistingFile);
    fs::path checkpoint;
    initGroup->add_option("-c,--checkpoint-file", checkpoint, "Checkpoint file")
        ->check(CLI::ExistingFile);
    initGroup->require_option(1);

    CLI11_PARSE(app, argc, argv);

    fmt::println("{}", app.get_description());

    if (config.empty()) {
        throw std::runtime_error{"Loading from checkpoint not supported currently!"};
    }
    auto mngr = taosim::simulation::SimulationManager::fromConfig(config);
    mngr->runSimulations();

    fmt::println(" - all simulations finished, exiting");

    return 0;
}

//-------------------------------------------------------------------------
