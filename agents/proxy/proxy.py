# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import json
import uvicorn
import asyncio
import aiohttp
import argparse
import msgspec
import os
import time
import copy
import bittensor as bt
from pathlib import Path
from threading import Thread

from fastapi import FastAPI, APIRouter, Request

from taos.common.neurons import BaseNeuron
from taos.im.neurons.validator import Validator
from taos.im.protocol import MarketSimulationStateUpdate, MarketSimulationConfig, FinanceAgentResponse, FinanceEventNotification
from taos.im.protocol.simulator import SimulatorResponseBatch
from taos.im.protocol.events import SimulationStartEvent

import xml.etree.ElementTree as ET
#--------------------------------------------------------------------------

class Proxy(Validator):
    def __init__(self, launcher_config):
        bt.logging.set_info()
        base_config = copy.deepcopy(BaseNeuron.config())
        self.config = self.config()
        self.config.merge(base_config)
        self.check_config(self.config)
        config_file = launcher_config['proxy']['simulation_xml']
        if not os.path.exists(config_file):
            raise Exception(f"Simulator config does not exist at {config_file}!")
        self.simulator_config_file = os.path.realpath(Path(config_file))
        self.simulation_config = self.load_simulation_config()
        
        self.agent_urls = {}
        port = config['agents']['start_port']
        for agent, agent_configs in config['agents'].items():
            if agent in ['start_port', 'path']: continue
            for agent_config in agent_configs:
                base_agent_name = f"{agent}_{'_'.join(list([str(a) for a in agent_config['params'].values()]))}"
                for i in range(agent_config['count']):
                    agent_name = f"{base_agent_name}_{i}"
                    self.agent_urls[agent_name] = f"http://127.0.0.1:{port}/handle"
                    port += 1                    
        
        self.compressing = False
        self.start_time = None
        self.start_timestamp = None
        self.step = 0
        
        # Add routes for methods receiving input from simulator
        self.router = APIRouter()
        self.router.add_api_route("/orderbook", self.orderbook, methods=["GET"])
        self.router.add_api_route("/account", self.account, methods=["GET"])

    def load_simulation_config(self):
        self.xml_config = ET.parse(self.simulator_config_file).getroot()
        self.simulation = MarketSimulationConfig.from_xml(self.xml_config)
    
    def onStart(self, timestamp, event : SimulationStartEvent) -> None:
        """
        Triggered when start of simulation event is published by simulator.
        Sets the simulation output directory and retrieves any fundamental price values already written.
        """
        bt.logging.info("-"*40)
        bt.logging.info("SIMULATION STARTED")
        self.start_time = time.time()
        self.simulation_timestamp = timestamp
        self.start_timestamp = self.simulation_timestamp
        self.simulation.logDir = event.logDir
        self.compress_outputs()
        bt.logging.info(f"START TIME: {self.start_time}")
        bt.logging.info(f"TIMESTAMP : {self.start_timestamp}")
        bt.logging.info(f"OUT DIR   : {self.simulation.logDir}")
        bt.logging.info("-"*40)

    async def orderbook(self, request : Request):
        body = await request.body()
        message = msgspec.json.decode(body)
        state = MarketSimulationStateUpdate.from_json(message) # Populate synapse class from request data
        if not self.start_time:
            self.start_time = time.time()
            self.start_timestamp = state.timestamp
        if self.simulation.logDir != message['payload']['logDir']:
            bt.logging.info(f"Simulation log directory changed : {self.simulation.logDir} -> {message['payload']['logDir']}")
            self.simulation.logDir = message['payload']['logDir']
        self.simulation_timestamp = state.timestamp
        if self.simulation:
            state.config = self.simulation.model_copy()
            state.config.logDir = None
        self.step += 1
        bt.logging.debug(f"STATE : {state}")

        # Forward state to agents
        async def query_agent(agent, url, session, json):
            try:
                bt.logging.info(f"Querying {agent} at {url}...")
                async with session.post(url=url, json=json, timeout=config['proxy']['timeout']) as r:
                    response = await r.json()
                    bt.logging.success(f"{agent} | Response : {response}")
                return agent, response
            except asyncio.exceptions.TimeoutError as e:
                bt.logging.error(f"{agent} | Timed out after {config['proxy']['timeout']}s while awaiting response from {url}.")
                return agent, None
            except Exception as e:
                bt.logging.error(f"{agent} | Failed to query {url}: {e}")
                return agent, None

        async with aiohttp.ClientSession() as session:
            responses = await asyncio.gather(*(query_agent(agent, agent_url, session, state.model_dump()) for agent, agent_url in self.agent_urls.items()))
        agent_responses = []
        for agent, response in responses:
            if response:
                try:
                    agent_responses.append(FinanceAgentResponse.model_validate(response))
                except Exception as e:
                    bt.logging.error(f"{agent} | Failed to validate response : {e}")
        simulator_response = SimulatorResponseBatch(agent_responses).serialize()
        return simulator_response

    async def account(self, request : Request):
        body = await request.body()
        batch = msgspec.json.decode(body)
        bt.logging.info(f"NOTICE : {batch}")
        for message in batch['messages']:
            if message['type'] == 'EVENT_SIMULATION_START':
                self.onStart(message['timestamp'], FinanceEventNotification.from_json(message).event)

#--------------------------------------------------------------------------
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', type=str, default="config.json")
    args = parser.parse_args()
    config = json.load(open(args.config))
    app = FastAPI()
    proxy = Proxy(config)
    app.include_router(proxy.router)
    # Start simulator price seeding data process in new thread
    Thread(target=proxy.seed, daemon=True, name='Seed').start()
    # Run the proxy as a FastAPI client via uvicorn on the configured port
    uvicorn.run(app, port=config['proxy']['port'])