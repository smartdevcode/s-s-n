# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
# The MIT License (MIT)

# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the “Software”), to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all copies or substantial portions of
# the Software.

# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
# THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

import os
import platform
import time
import argparse
import torch
import traceback
import json
import xml.etree.ElementTree as ET
import pandas as pd

# Bittensor
import bittensor as bt

import uvicorn
from typing import Tuple
from fastapi import FastAPI, APIRouter
from fastapi import Request
from threading import Thread

import subprocess
import psutil
from git import Repo
from pathlib import Path

from binance.websocket.spot.websocket_stream import SpotWebsocketStreamClient
from taos.im.utils.coinbase import CoinbaseClient
from coinbase.websocket import WSClientConnectionClosedException,WSClientException

from taos.common.neurons.validator import BaseValidatorNeuron
from taos.common.utils.misc import run_process

from taos.im.protocol.simulator import SimulatorBookMessage, SimulatorResponseBatch, SimulatorMessageBatch
from taos.im.protocol import MarketSimulationStateUpdate, FinanceEventNotification, FinanceAgentResponse
from taos.im.protocol.models import MarketSimulationConfig
from taos.im.protocol.events import SimulationStartEvent

class Validator(BaseValidatorNeuron):
    """
    intelligent market simulation validator implementation.

    The validator is run as a FastAPI client in order to receive messages from the simulator engine for processing and forwarding to miners.
    Metagraph maintenance, weight setting, state persistence and other general bittensor routines are executed in a separate thread.
    The validator also handles publishing of metrics via Prometheus for visualization and analysis, as well as retrieval and recording of seed data for simulation price process generation.
    """

    @classmethod
    def add_args(cls, parser: argparse.ArgumentParser) -> None:
        """
        Add intelligent-markets-specific validator configuration arguments.
        """
        add_im_validator_args(cls, parser)

    def maintain(self) -> None:
        """
        Maintains the metagraph and sets weights.
        This method is run in a separate thread parallel to the FastAPI server.
        """
        while True:
            try:
                bt.logging.debug("Synchronizing...")
                self.sync(save_state=False)
                if not self.check_simulator():
                    self.restart_simulator()
                time.sleep(bt.settings.BLOCKTIME * 10)
            except Exception as ex:
                bt.logging.error(f"Failed to sync : {traceback.print_exc()}")

    def seed(self) -> None:
        """
        Retrieve price data for use as simulation fundamental price seed, and record to simulator-accessible location.
        This process is also run in a separate thread parallel to the FastAPI server.
        """
        try:
            self.seed_count = 0
            self.seed_filename = None
            last_count = self.seed_count
            self.last_seed = None
            self.pending_seed_data = ''
            self.seed_exchange = 'coinbase'
            self.seed_client = 'coinbase'

            def binance_seed(_, message : str) -> None:
                try:
                    # bt.logging.trace(f"BI SEED {self.seed_count} : {message}")
                    message_dict = json.loads(message)
                    if 'e' in message_dict and message_dict['e'] == 'trade':
                        seed = float(message_dict['p'])
                        record_seed(seed)
                except Exception as ex:
                    bt.logging.error(f"Exception getting Binance seed value : Message={message} | Error={ex}")

            def coinbase_seed(message : str) -> None:
                try:
                    # bt.logging.trace(f"CB SEED {self.seed_count} : {message}")
                    message_dict = json.loads(message)
                    if 'channel' in message_dict and message_dict['channel'] == 'market_trades':
                        for event in message_dict['events']:
                            if event['type'] == 'update' and 'trades' in event:
                                seed = float(event['trades'][0]['price'])
                                record_seed(seed)
                                return
                except Exception as ex:
                    bt.logging.error(f"Exception getting Coinbase seed value : Message={message} | Error={ex}")

            def record_seed(seed : int) -> None:
                try:
                    if not self.last_seed or self.last_seed != seed:
                        self.seed_count += 1
                        if not self.simulation.logDir:
                            self.pending_seed_data += f"{self.seed_count},{seed}\n"
                        else:
                            if self.seed_filename != os.path.join(self.simulation.logDir,"fundamental_seed.csv"):
                                self.last_seed = None
                                self.seed_count = 0
                                self.pending_seed_data = ''
                            if not self.last_seed:
                                self.seed_filename = os.path.join(self.simulation.logDir,"fundamental_seed.csv")
                                if os.path.exists(self.seed_filename) and os.stat(self.seed_filename).st_size > 0:
                                    with open(self.seed_filename) as f:
                                        for line in f:
                                            pass
                                        self.seed_count = self.seed_count + int(line.split(',')[0])
                                self.seed_file = open(self.seed_filename,'a')
                                self.seed_file.write(self.pending_seed_data)
                                self.pending_seed_data = ''
                            self.seed_file.write(f"{self.seed_count},{seed}\n")
                            self.seed_file.flush()
                            self.last_seed = seed
                except Exception as ex:
                    bt.logging.error(f"Exception in seed handling : Seed={seed} | Error={ex}")

            def connect() -> None:
                attempts = 0
                while True:
                    try:
                        attempts += 1
                        self.seed_exchange='coinbase'
                        self.seed_client = CoinbaseClient(on_message=coinbase_seed)
                        bt.logging.info("Attempting to connect to Coinbase Trades Stream...")
                        self.seed_client.open()
                        self.seed_client.subscribe(product_ids=["BTC-USD"], channels=["market_trades"])
                        bt.logging.success("Subscribed to Coinbase Trades Stream!")
                        break
                    except Exception as ex:
                        bt.logging.warning(f"Unable to connect to Coinbase Trades Stream! {ex}. Trying Binance.")
                        try:
                            self.seed_exchange='binance'
                            self.seed_client = SpotWebsocketStreamClient(on_message=binance_seed)
                            self.seed_client.trade(symbol=validator.config.simulation.seed_symbol)
                            bt.logging.success("Subscribed to Binance Trades Stream!")
                            break
                        except Exception as ex:
                            bt.logging.error(f"Unable to connect to Binance Trades Stream : {ex}.")
                            bt.logging.error(f"Failed connecting to fundamental price seed stream (Attempt {attempts})")

            connect()

            while True:
                try:
                    if self.seed_exchange=='coinbase':
                        try:
                            self.seed_client.sleep_with_exception_check(10)
                        except WSClientException as e:
                            bt.logging.warning(f"Error in Coinbase websocket : {e}")
                        except WSClientConnectionClosedException as e:
                            bt.logging.error("Coinbase connection closed! Sleeping for 10 seconds before reconnecting...")
                            time.sleep(10)
                        if not self.seed_client._is_websocket_open():
                            connect()
                    else:
                        time.sleep(10)
                    if self.last_seed:
                        self.seed_file.flush()
                        if last_count == self.seed_count:
                            bt.logging.warning(f"No new seed in last 10 seconds!  Restarting connection.")
                            if self.seed_exchange=='coinbase' and self.seed_client._is_websocket_open():
                                self.seed_client.close()
                            connect()
                        last_count = self.seed_count
                except Exception as ex:
                    bt.logging.error(f"Exception in seed loop : {ex}")
        except Exception as ex:
            bt.logging.error(f"Failure in seeding process : {ex}")

    def update_repo(self, end=False) -> Tuple[bool, bool, bool, bool]:
        """
        Checks for and pulls latest changes from the taos repo.
        If source has changed, the simulator is rebuilt and restarted.
        If config has changed, restart the simulator to activate the new parameterizations.
        If validator source is updated, restart validator process.
        """
        try:
            self.repo = Repo(self.repo_path)
            remote = self.repo.remotes[self.config.repo.remote]
            fetch = remote.fetch('main')
            diff = self.repo.head.commit.diff(remote.refs['main'].object.hexsha)
            validator_py_files_changed = False
            simulator_config_changed = False
            simulator_py_files_changed = False
            simulator_cpp_files_changed = False
            for cht in diff.change_type:
                changes = list(diff.iter_change_type(cht))
                for c in changes:
                    if str(self.repo_path / c.b_path) == self.simulator_config_file:
                        simulator_config_changed = True
                    if c.b_path.endswith('.cpp'):
                        simulator_cpp_files_changed = True
                    if c.b_path.endswith('.py'):
                        if 'simulate/trading' in c.b_path:
                            simulator_py_files_changed = True
                        else:
                            validator_py_files_changed = True
            if len(diff) > 0:
                pull = remote.pull()
            if simulator_cpp_files_changed or simulator_py_files_changed:
                bt.logging.warning("SIMULATOR SOURCE CHANGED")
                self.rebuild_simulator()
            if simulator_config_changed:
                bt.logging.warning("SIMULATOR CONFIG CHANGED")
            if end or validator_py_files_changed or simulator_cpp_files_changed or simulator_py_files_changed or simulator_config_changed:
                self.restart_simulator()
            if validator_py_files_changed:
                self.update_validator()
            return validator_py_files_changed, simulator_config_changed, simulator_py_files_changed, simulator_cpp_files_changed
        except Exception as ex:
            traceback.print_exc()
            bt.logging.error(f"Failed to update repo : {ex}")
            return False, False

    def update_validator(self) -> None:
        """
        Updates, installs and restarts validator.
        """
        py_cmd = ["pip", "install", "-e", "."]
        bt.logging.info(f"UPDATING VALIDATOR (PY)...")
        make = run_process(py_cmd, cwd=(self.repo_path ).resolve())
        if make.returncode == 0:
            bt.logging.success("VALIDATOR PY UPDATE SUCCESSFUL.  RESTARTING...")
        else:
            raise Exception(f"FAILED TO COMPLETE VALIDATOR PY UPDATE:\n{make.stderr}")

        pm2_json = subprocess.run(['pm2', 'jlist'],capture_output = True, text = True).stdout
        pm2_js = json.loads(pm2_json) if pm2_json else []
        restart_cmd = None
        if len(pm2_js) > 0:
            pm2_processes = {p['name'] : p for p in pm2_js}
            if 'validator' in pm2_processes:
                bt.logging.info("FOUND VALIDATOR IN pm2 PROCESSES.")
                restart_cmd = ["pm2 restart validator"]
        if not restart_cmd:
            for proc in psutil.process_iter():
                if 'python validator.py' in ' '.join(proc.cmdline()):
                    bt.logging.info(f"FOUND VALIDATOR PROCESS `{proc.name()}` WITH PID {proc.pid}")
                    proc.kill()
            restart_cmd = [f"pm2 start --name=validator \"python validator.py --netuid {self.config.netuid} --subtensor.chain_endpoint {self.config.subtensor.chain_endpoint} --wallet.path {self.config.wallet.path} --wallet.name {self.config.wallet.name} --wallet.hotkey {self.config.wallet.hotkey}\""]
        bt.logging.info(f"RESTARTING VALIDATOR : {' '.join(restart_cmd)}")
        validator = subprocess.run(restart_cmd, cwd=str((self.repo_path / 'taos' / 'im' / 'neurons').resolve()), shell=True, capture_output=True)
        return
        # if validator.returncode == 0:
        #     bt.logging.info("VALIDATOR RESTART SUCCESSFUL.")
        # else:
        #     raise Exception(f"FAILED TO RESTART VALIDATOR:\nSTDOUT : {validator.stdout}\nSTDERR : {validator.stderr}")

    def rebuild_simulator(self) -> None:
        """
        Re-compiles the C++ simulator.
        """
        gcc_version_proc = subprocess.run(['g++ -dumpversion'], shell=True, capture_output=True)
        if gcc_version_proc.returncode == 0:
            gcc_version = gcc_version_proc.stdout
            gcc14_check_proc = subprocess.run(['g++-14 -dumpversion'], shell=True, capture_output=True)
            if gcc14_check_proc.returncode != 0:
                raise Exception(f"Could not find g++-14 on system : {gcc14_check_proc.stderr}")
        else:
            raise Exception(f"Could not find g++ version : {gcc_version_proc.stderr}")
        if gcc_version != '14':
            make_cmd = ["cmake","-DENABLE_TRACES=1", "-DCMAKE_BUILD_TYPE=Release", "..", "-D", "CMAKE_CXX_COMPILER=g++-14"]
        else:
            make_cmd = ["cmake","-DENABLE_TRACES=1", "-DCMAKE_BUILD_TYPE=Release", ".."]

        bt.logging.info(f"REBUILDING SIMULATOR (MAKE)...")
        make = run_process(make_cmd, (self.repo_path / 'simulate' / 'trading' / 'build').resolve())
        if make.returncode == 0:
            bt.logging.success("MAKE PROCESS SUCCESSFUL.  BUILDING...")
            build_cmd = ["cmake", "--build","."]
            bt.logging.info(f"REBUILDING SIMULATOR (BUILD)...")
            build = run_process(build_cmd, cwd=(self.repo_path / 'simulate' / 'trading' / 'build').resolve())
            if build.returncode == 0:
                bt.logging.success("REBUILT SIMULATOR.")
            else:
                raise Exception(f"FAILED TO COMPLETE SIMULATOR BUILD:\n{build.stderr}")
        else:
            raise Exception(f"FAILED TO COMPLETE SIMULATOR MAKE:\n{make.stderr}")

        py_cmd = ["pip", "install", "-e", "."]
        bt.logging.info(f"REBUILDING SIMULATOR (PY)...")
        py = run_process(py_cmd, cwd=(self.repo_path / 'simulate' / 'trading').resolve())
        if py.returncode == 0:
            bt.logging.success("PY UPDATE SUCCESSFUL.")
        else:
            raise Exception(f"FAILED TO COMPLETE SIMULATOR PY UPDATE:\n{make.stderr}")

    def restart_simulator(self) -> None:
        """
        Restarts the C++ simulator process.
        """
        pm2_json = subprocess.run(['pm2', 'jlist'],capture_output = True, text = True).stdout
        pm2_js = json.loads(pm2_json) if pm2_json else []

        restart_cmd = None
        if len(pm2_js) > 0:
            pm2_processes = {p['name'] : p for p in pm2_js}
            if 'simulator' in pm2_processes:
                bt.logging.info("FOUND SIMULATOR IN pm2 PROCESSES.")
                restart_cmd = ["pm2 restart simulator"]
        if not restart_cmd:
            for proc in psutil.process_iter():
                if '../build/src/cpp/taosim' in ' '.join(proc.cmdline()):
                    bt.logging.info(f"FOUND SIMULATOR PROCESS `{proc.name()}` WITH PID {proc.pid}")
                    proc.kill()
            restart_cmd = [f"pm2 start --no-autorestart --name=simulator \"../build/src/cpp/taosim -f {self.simulator_config_file}\""]
        bt.logging.info(f"RESTARTING SIMULATOR : {' '.join(restart_cmd)}")
        simulator = subprocess.run(restart_cmd, cwd=str((self.repo_path / 'simulate' / 'trading' / 'run').resolve()), shell=True, capture_output=True)
        if simulator.returncode == 0:
            if self.check_simulator():
                bt.logging.success("SIMULATOR RESTART SUCCESSFUL.")
            else:
                bt.logging.error("FAILED TO RESTART SIMULATOR!  NOT FOUND IN PM2 AFTER RESTART.")                
        else:
            raise Exception(f"FAILED TO RESTART SIMULATOR:\nSTDOUT : {simulator.stdout}\nSTDERR : {simulator.stderr}")

    def check_simulator(self) -> bool:
        """
        If no new state update has been retrieved for 5 minutes, check if the simulator process is still running.
        If the simulator is found not to be online, restart it.
        TODO: Use checkpointing functionality to resume latest simulation.
        """
        if self.last_state_time and self.last_state_time < time.time() - 300:
            pm2_json = subprocess.run(['pm2', 'jlist'],capture_output = True, text = True).stdout
            pm2_js = json.loads(pm2_json) if pm2_json else []
            pm2_processes = {p['name'] : p for p in pm2_js}
            if 'simulator' in pm2_processes:
                if pm2_processes['simulator']['pm2_env']['status'] != 'online':                
                    bt.logging.error(f"Simulator process (PM2) has stopped!")
                    return False
                else: 
                    return True
            else:
                found = False
                for proc in psutil.process_iter():
                    if '../build/src/cpp/taosim' in ' '.join(proc.cmdline()):
                        found = True
                if not found:
                    bt.logging.error(f"Simulator process (No PM2) has stopped!")
                    return False
                else: 
                    return True
        return True

    def save_state(self) -> None:
        """Saves the state of the validator to a file."""
        bt.logging.trace("Saving validator state.")

        # Save the state of the validator to file.
        torch.save(
            {
                "step": self.step,
                "start_time": self.start_time,
                "start_timestamp": self.start_timestamp,
                "step_rates": self.step_rates,
                "scores": self.scores,
                "hotkeys": self.hotkeys,
                "sharpe_values": self.sharpe_values,
                "unnormalized_scores": self.unnormalized_scores,
                "inventory_history": self.inventory_history,
                "recent_trades": self.recent_trades,
                "pending_notices": self.pending_notices,
                "simulation.logDir": self.simulation.logDir,
                "fundamental_price": self.fundamental_price,
                "trades" : self.trades,
            },
            self.state_file,
        )

    def load_state(self) -> None:
        """Loads the state of the validator from a file."""
        if not self.config.neuron.reset and os.path.exists(self.state_file):
            bt.logging.info(f"Loading validator state from {self.state_file}.")
            state = torch.load(self.state_file, weights_only=False)
            self.step = state["step"]
            self.start_time = state["start_time"]
            self.start_timestamp = state["start_timestamp"]
            self.step_rates = state["step_rates"]
            self.scores = state["scores"]
            self.hotkeys = state["hotkeys"]
            self.pending_notices = state["pending_notices"]
            self.sharpe_values = state["sharpe_values"]
            self.unnormalized_scores = state["unnormalized_scores"]
            self.inventory_history = state["inventory_history"]
            self.recent_trades = state["recent_trades"]
            self.simulation.logDir = state["simulation.logDir"]
            self.fundamental_price = state["fundamental_price"]
            self.trades = state["trades"] if "trades" in state else {uid : {bookId : {} for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
        else:
            # If no state exists or the neuron.reset flag is set, re-initialize the validator state
            if self.config.neuron.reset and os.path.exists(self.state_file):
                bt.logging.warning(f"`neuron.reset is True, ignoring previous state info at {self.state_file}.")
            else:
                bt.logging.info(f"No previous state information at {self.state_file}, initializing new state.")
            self.pending_notices = {uid : [] for uid in range(self.subnet_info.max_uids)}
            self.sharpe_values = {uid :
                {
                    'books' : {
                        bookId : 0.0 for bookId in range(self.simulation.book_count)
                    },
                    'total' : 0.0
                } for uid in range(self.subnet_info.max_uids)
            }
            self.unnormalized_scores = {uid : 0.0 for uid in range(self.subnet_info.max_uids)}
            self.inventory_history = {uid : {} for uid in range(self.subnet_info.max_uids)}
            self.recent_trades = {bookId : [] for bookId in range(self.simulation.book_count)}
            self.fundamental_price = {bookId : None for bookId in range(self.simulation.book_count)}
            self.trades = {uid : {bookId : {} for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}

    def load_simulation_config(self) -> None:
        """
        Reads elements from the config XML to populate the simulation config class object.
        """
        self.xml_config = ET.parse(self.config.simulation.xml_config).getroot()
        Init_config = self.xml_config.find('Agents').find('InitializationAgent')
        STA_config = self.xml_config.find('Agents').find('StylizedTraderAgent')
        HFT_config = self.xml_config.find('Agents').find('HighFrequencyTraderAgent')
        self.simulation = MarketSimulationConfig(
            time_unit = str(self.xml_config.attrib['timescale']),
            duration = int(self.xml_config.attrib['duration']),
            grace_period = int(self.xml_config.find('Agents').find('MultiBookExchangeAgent').attrib['gracePeriod']),
            publish_interval = int(self.xml_config.attrib['step']),

            book_count = int(self.xml_config.find('Agents').find('MultiBookExchangeAgent').find('Books').attrib['instanceCount']),
            book_levels = int(self.xml_config.find('Agents').find('MultiBookExchangeAgent').find('Books').attrib['maxDepth']),

            start_base_balance = float(self.xml_config.find('Agents').find('MultiBookExchangeAgent').find('Balances').find('Base').attrib['total']),
            start_quote_balance = float(self.xml_config.find('Agents').find('MultiBookExchangeAgent').find('Balances').find('Quote').attrib['total']),
            baseDecimals = int(self.xml_config.find('Agents').find('MultiBookExchangeAgent').attrib['baseDecimals']),
            quoteDecimals = int(self.xml_config.find('Agents').find('MultiBookExchangeAgent').attrib['quoteDecimals']),
            priceDecimals = int(self.xml_config.find('Agents').find('MultiBookExchangeAgent').attrib['priceDecimals']),
            volumeDecimals = int(self.xml_config.find('Agents').find('MultiBookExchangeAgent').attrib['volumeDecimals']),

            init_agent_count = int(Init_config.attrib['instanceCount']),
            init_price = int(Init_config.attrib['price']),

            fp_GBM_mu = float(STA_config.attrib['GBM_mu']),
            fp_GBM_sigma = float(STA_config.attrib['GBM_sigma']),
            fp_GBM_lambda_jump = float(STA_config.attrib['GBM_lambda_jump']),
            fp_GBM_mu_jump = float(STA_config.attrib['GBM_mu_jump']),
            fp_GBM_sigma_jump = float(STA_config.attrib['GBM_sigma_jump']),
            fp_GBM_flag_jump = int(STA_config.attrib['GBM_flag_jump']),
            fp_GBM_seed = int(STA_config.attrib['GBM_seed']),

            sta_agent_count = int(STA_config.attrib['instanceCount']),
            sta_noise_agent_weight = float(STA_config.attrib['sigmaN']),
            sta_chartist_agent_weight = float(STA_config.attrib['sigmaC']),
            sta_fundamentalist_agent_weight = float(STA_config.attrib['sigmaF']),
            sta_tau = int(STA_config.attrib['tau']),
            sta_sigmaEps = float(STA_config.attrib['sigmaEps']),
            sta_r_aversion = float(STA_config.attrib['r_aversion']),

            hft_agent_count = int(HFT_config.attrib['instanceCount'] if HFT_config else 0),
            hft_tau = int(HFT_config.attrib['tau']) if HFT_config else None,
            hft_delta = int(HFT_config.attrib['delta']) if HFT_config else None,
            hft_psiHFT = float(HFT_config.attrib['psiHFT_constant']) if HFT_config else None,
            hft_gHFT = float(HFT_config.attrib['gHFT']) if HFT_config else None
        )
        self.state_file = self.config.neuron.full_path + f"/{self.simulation.label()}.pt"

    def __init__(self, config=None) -> None:
        """
        Initialize the intelligent markets simulation validator.
        """
        super(Validator, self).__init__(config=config)
        # Load the simulator config XML file data in order to make context and parameters accessible for reporting and output location.

        if not os.path.exists(self.config.simulation.xml_config):
            raise Exception(f"Simulator config does not exist at {self.config.simulation.xml_config}!")
        self.simulator_config_file = os.path.realpath(Path(self.config.simulation.xml_config))
        self.load_simulation_config()
        self.repo_path = Path(os.path.dirname(os.path.realpath(__file__))).parent.parent.parent
        if self.config.neuron.reset or not os.path.exists(self.state_file):
            self.update_repo()
        self.start_time = None
        self.start_timestamp = None
        self.last_state_time = None
        self.step_rates = []
        self.reporting = False

        # Add routes for methods receiving input from simulator
        self.router = APIRouter()
        self.router.add_api_route("/orderbook", self.orderbook, methods=["GET"])
        self.router.add_api_route("/account", self.account, methods=["GET"])
        # Initialize subnet info and other basic validator/simulation properties
        self.subnet_info = self.subtensor.get_metagraph_info(self.config.netuid)
        self.last_state = None
        self.simulation_timestamp = 0
        self.reward_weights = {"sharpe" : 1.0}

        self.load_state()

        self.miner_stats = {uid : {'requests' : 0, 'timeouts' : 0, 'failures' : 0, 'rejections' : 0, 'call_time' : []} for uid in range(self.subnet_info.max_uids)}
        init_metrics(self)
        publish_info(self)

    def onStart(self, timestamp, event : SimulationStartEvent) -> None:
        """
        Triggered when start of simulation event is published by simulator.
        Sets the simulation output directory and retrieves any fundamental price values already written.
        """
        bt.logging.info("-"*40)
        bt.logging.info("SIMULATION STARTED")
        self.trades = {uid : {bookId : {prev_time - self.simulation_timestamp : volume for prev_time, volume in self.trades[uid][bookId].items()} for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
        self.start_time = time.time()
        self.simulation_timestamp = timestamp
        self.start_timestamp = self.simulation_timestamp
        self.load_simulation_config()
        self.last_state_time = None
        self.step_rates = []
        self.simulation.logDir = event.logDir
        bt.logging.info(f"START TIME: {self.start_time}")
        bt.logging.info(f"TIMESTAMP : {self.start_timestamp}")
        bt.logging.info(f"OUT DIR   : {self.simulation.logDir}")
        bt.logging.info("-"*40)
        df_fp = pd.read_csv(os.path.join(self.simulation.logDir,'fundamental.csv'))
        df_fp.set_index('Timestamp')
        self.fundamental_price = {bookId : df_fp[str(bookId)].to_dict() for bookId in range(self.simulation.book_count)}
        self.recent_trades = {bookId : [] for bookId in range(self.simulation.book_count)}
        self.save_state()

    def onEnd(self) -> None:
        """
        Triggered when end of simulation event is published by simulator.
        Resets quantities as necessary, updates, rebuilds and launches simulator with the latest configuration.
        """
        bt.logging.info("SIMULATION ENDED")
        self.simulation.logDir = None
        self.fundamental_price = {bookId : None for bookId in range(self.simulation.book_count)}
        self.pending_notices = {uid : [] for uid in range(self.subnet_info.max_uids)}
        self.save_state()
        self.update_repo(end=True)

    def handle_deregistration(self, uid) -> None:
        """
        Triggered on deregistration of a UID.
        Zeroes scores and flags the UID for resetting of account state in simulator.
        """
        self.sharpe_values[uid] = {
            'books' : {
                bookId : 0.0 for bookId in range(self.simulation.book_count)
            },
            'total' : 0.0
        }
        self.unnormalized_scores[uid] = 0.0
        self.inventory_history[uid] = {}
        self.deregistered_uids.append(uid)
        self.trades[uid] = {bookId : {} for bookId in range(self.simulation.book_count)}
        bt.logging.debug(f"UID {uid} Deregistered - Scheduled for reset.")

    def report(self) -> None:
        """
        Publish performance and state metrics.
        """
        if not self.reporting:
            Thread(target=report, args=(self,), daemon=True, name=f'report_{self.step}').start()
        # report(self)

    async def orderbook(self, request : Request) -> dict:
        """
        The route method which receives and processes simulation state updates received from the simulator.
        """
        data = await request.json()
        message = SimulatorBookMessage.model_validate(data) # Validate the state update message and populate class object
        if not self.start_time:
            self.start_time = time.time()
            self.start_timestamp = message.timestamp
        if self.simulation.logDir != message.payload.logDir:
            bt.logging.info(f"Simulation log directory changed : {self.simulation.logDir} -> {message.payload.logDir}")
            self.simulation.logDir = message.payload.logDir
        self.simulation_timestamp = message.timestamp
        self.step_rates.append((message.timestamp - (self.last_state.timestamp if self.last_state else self.start_timestamp)) / (time.time() - (self.last_state_time if self.last_state_time else self.start_time)))
        state = MarketSimulationStateUpdate.from_simulator(self.simulation_timestamp, message.payload) # Transform simulator message class to network synapse
        self.last_state = state
        if self.simulation:
            state.config = self.simulation.model_copy()
            state.config.logDir = None
        self.step += 1
        # Log received state data
        bt.logging.info(f"STATE UPDATE RECEIVED | VALIDATOR STEP : {self.step} | TIMESTAMP : {data['timestamp']}")
        debug_text = ''
        for bookId, book in state.books.items():
            debug_text += '-' * 50 + "\n"
            debug_text += f"BOOK {bookId}" + "\n"
            if book.bids and book.asks:
                debug_text += ' | '.join([f"{level.quantity:.4f}@{level.price}" for level in reversed(book.bids[:5])]) + '||' + ' | '.join([f"{level.quantity:.4f}@{level.price}" for level in book.asks[:5]]) + "\n"
            else:
                debug_text += "EMPTY" + "\n"
        bt.logging.debug("\n" + debug_text.strip("\n"))
        # Forward state synapse to miners, populate response data to simulator object and serialize for returning to simulator.
        for notice in state.notices[self.uid]:
            if notice.type == "RESPONSE_DISTRIBUTED_RESET_AGENT" or notice.type == "ERROR_RESPONSE_DISTRIBUTED_RESET_AGENT":
                for reset in notice.resets:
                    if reset.success:
                        bt.logging.info(f"Agent {reset.agentId} Balances Reset! {reset}")                        
                        self.deregistered_uids.remove(reset.agentId)
                    else:
                        bt.logging.error(f"Failed to Reset Agent {reset.agentId} : {reset.message}")
        while self.reporting:
            bt.logging.debug(f"Waiting for reporting to complete...")
            time.sleep(1)
        response = SimulatorResponseBatch(await forward(self, state)).serialize()

        if len(response['responses']) > 0:
            bt.logging.trace(f"RESPONSE : {response}")
        bt.logging.info(f"RATE : {self.step_rates[-1] / 1e9:.2f} STEPS/s | AVG : {sum(self.step_rates) / len(self.step_rates) / 1e9:.2f}  STEPS/s")
        self.step_rates = self.step_rates[-10000:]
        self.last_state_time = time.time()
        self.save_state()
        self.report()
        for notice in state.notices[0]:
            if notice.type == 'EVENT_SIMULATION_STOP':
                self.onEnd()
        return response

    async def account(self, request : Request) -> None:
        """
        The route method which receives event notification messages from the simulator.
        This method is currently used only to enable the simulation start message to be immediately propagated to the validator.
        Other events are instead recorded to the simulation state object.
        """
        data = await request.json()
        batch = SimulatorMessageBatch.model_validate(data) # Validate the simulator message and load to class object
        bt.logging.info(f"NOTICE : {batch}")
        notices = []
        for message in batch.messages:
            if message.type == 'EVENT_SIMULATION_START':
                self.onStart(message.timestamp, FinanceEventNotification.from_simulator(message).event)
            elif message.type == 'EVENT_SIMULATION_STOP':
                self.onEnd()
            else:
                notice = FinanceEventNotification.from_simulator(message)
                if not notice:
                    bt.logging.error(f"Unrecognized notification : {message}")
                else:
                    notices.append(notice.event)
        await notify(self, notices) # This method forwards the event notifications to the related miners.

# The main method which runs the validator
if __name__ == "__main__":
    from taos.im.validator.forward import forward, notify
    from taos.im.validator.report import report, publish_info, init_metrics
    from taos.im.config import add_im_validator_args
    if float(platform.freedesktop_os_release()['VERSION_ID']) < 22.04:
        raise Exception(f"taos validator requires Ubuntu >= 22.04!")
    # Initialize FastAPI client and attach validator router
    app = FastAPI()
    validator = Validator()
    app.include_router(validator.router)
    # Start validator maintenance loop in new thread
    Thread(target=validator.maintain, daemon=True, name='Sync').start()
    # Start simulator price seeding data process in new thread
    Thread(target=validator.seed, daemon=True, name='Seed').start()
    # Run the validator as a FastAPI client via uvicorn on the configured port
    uvicorn.run(app, port=validator.config.port)
