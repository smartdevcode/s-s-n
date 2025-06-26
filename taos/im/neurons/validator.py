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
import xml.etree.ElementTree as ET
import pandas as pd
import msgpack
import msgspec
import math
import shutil
import zipfile
import asyncio
from datetime import datetime

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

from taos.common.neurons.validator import BaseValidatorNeuron

from taos.im.config import add_im_validator_args
from taos.im.protocol.simulator import SimulatorResponseBatch
from taos.im.protocol import MarketSimulationStateUpdate, FinanceEventNotification
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

    def _maintain(self) -> None:
        try:
            self.maintaining = True
            bt.logging.info(f"Synchronizing at Step {self.step}...")
            start=time.time()
            self.sync(save_state=False)
            if not check_simulator(self):
                restart_simulator(self)
            bt.logging.info(f"Synchronized ({time.time()-start:.4f}s)")
        except Exception as ex:
            bt.logging.error(f"Failed to sync : {traceback.format_exc()}")
        finally:
            self.maintaining = False
    
    def maintain(self) -> None:        
        """
        Maintains the metagraph and sets weights.
        This method is run in a separate thread to speed up processing.
        """
        if not self.maintaining:
            Thread(target=self._maintain, args=(), daemon=True, name=f'maintain_{self.step}').start()

    def seed(self) -> None:
        from taos.im.validator.seed import seed
        seed(self)

    def update_repo(self, end=False) -> bool:
        """
        Checks for and pulls latest changes from the taos repo.
        If source has changed, the simulator is rebuilt and restarted.
        If config has changed, restart the simulator to activate the new parameterizations.
        If validator source is updated, restart validator process.
        """
        try:
            validator_py_files_changed, simulator_config_changed, simulator_py_files_changed, simulator_cpp_files_changed = check_repo(self)
            remote = self.repo.remotes[self.config.repo.remote]

            if not end:
                if validator_py_files_changed and not (simulator_cpp_files_changed or simulator_py_files_changed):
                    bt.logging.warning("VALIDATOR LOGIC UPDATED - PULLING AND DEPLOYING.")
                    remote.pull()
                    update_validator(self)
            else:
                remote.pull()
                if simulator_cpp_files_changed or simulator_py_files_changed:
                    bt.logging.warning("SIMULATOR SOURCE CHANGED")
                    rebuild_simulator(self)
                if simulator_config_changed:
                    bt.logging.warning("SIMULATOR CONFIG CHANGED")
                restart_simulator(self)
                if validator_py_files_changed:
                    update_validator(self)
            return True
        except Exception as ex:
            self.pagerduty_alert(f"Failed to update repo : {ex}", details={"traceback" : traceback.format_exc()})
            return False

    def _compress_outputs(self,  start=False):
        self.compressing = True
        try:
            if self.simulation.logDir:
                log_root = Path(self.simulation.logDir).parent
                for output_dir in log_root.iterdir():
                    if output_dir.is_dir():
                        log_archives = {}
                        log_path = Path(output_dir)
                        for log_file in log_path.iterdir():
                            if log_file.is_file() and log_file.suffix == '.log':
                                log_period = log_file.name.split('.')[1]
                                log_end = (int(log_period.split('-')[1][:2]) * 3600 + int(log_period.split('-')[1][2:4]) * 60 + int(log_period.split('-')[1][5:])) * 1_000_000_000
                                if log_end < self.simulation_timestamp or (start and str(output_dir.resolve()) != self.simulation.logDir):
                                    log_type = log_file.name.split('-')[0]
                                    label = f"{log_type}_{log_period}"
                                    if not label in log_archives:
                                        log_archives[label] = []
                                    log_archives[label].append(log_file)
                        for label, log_files in log_archives.items():
                            archive = log_path / f"{label}.zip"
                            bt.logging.info(f"Compressing {label} files to {archive.name}...")
                            with zipfile.ZipFile(archive, "w" if not archive.exists() else "a", compression=zipfile.ZIP_DEFLATED) as zipf:
                                for log_file in log_files:
                                    try:
                                        zipf.write(log_file, arcname=Path(log_file).name)
                                        os.remove(log_file)
                                        bt.logging.debug(f"Added {log_file.name} to {archive.name}")
                                    except Exception as ex:
                                        bt.logging.error(f"Failed to add {log_file.name} to {archive.name} : {ex}")
                        if start and str(output_dir.resolve()) != self.simulation.logDir:
                            size = sum(file.stat().st_size for file in output_dir.rglob('*'))
                            bt.logging.info(f"Compressing output directory {output_dir.name} ({int(size / 1024 / 1024)}MB)...")
                            try:
                                shutil.make_archive(output_dir, 'zip', output_dir)
                                bt.logging.success(f"Compressed {output_dir.name} to {output_dir.name + '.zip'}.")
                            except Exception as ex:
                                self.pagerduty_alert(f"Failed to compress folder {output_dir.name} : {ex}", details={"trace" : traceback.format_exc()})
                                continue
                            try:
                                shutil.rmtree(output_dir)
                                bt.logging.success(f"Deleted {output_dir.name}.")
                            except Exception as ex:
                                self.pagerduty_alert(f"Failed to remove compressed folder {output_dir.name} : {ex}", details={"trace" : traceback.format_exc()})
                if psutil.disk_usage('/').percent > 90:
                    first_day_of_month = int(datetime.today().replace(day=1).strftime("%Y%m%d"))
                    bt.logging.warning(f"Disk usage > 90% - cleaning up old archives...")
                    for output_archive in sorted(log_root.iterdir(), key=lambda f: f.name[:13]):
                        try:
                            archive_date = int(output_archive.name[:8])
                        except:
                            continue
                        if output_archive.is_file() and output_archive.name.endswith('.zip') and archive_date < first_day_of_month:
                            try:
                                output_archive.unlink()
                                disk_usage = psutil.disk_usage('/').percent
                                bt.logging.success(f"Deleted {output_dir.name} ({disk_usage}% disk available).")
                                if disk_usage <= 90:
                                    break
                            except Exception as ex:
                                self.pagerduty_alert(f"Failed to remove archive {output_archive.name} : {ex}", details={"trace" : traceback.format_exc()})
                        
                            
        except Exception as ex:
            self.pagerduty_alert(f"Failure during output compression : {ex}", details={"trace" : traceback.format_exc()})
        finally:
            self.compressing = False

    def compress_outputs(self, start=False):
        if not self.compressing:
            Thread(target=self._compress_outputs, args=(start,), daemon=True, name=f'compress_{self.step}').start()

    def _save_state(self) -> None:
        """Saves the state of the validator to a file."""
        self.saving = True
        try:
            bt.logging.info("Saving simulation state...")
            start = time.time()
            # Save the state of the simulation to file.
            torch.save(
                {
                    "start_time": self.start_time,
                    "start_timestamp": self.start_timestamp,
                    "step_rates": self.step_rates,
                    "initial_balances": self.initial_balances,
                    "recent_trades": self.recent_trades,
                    "recent_miner_trades": self.recent_miner_trades,
                    "pending_notices": self.pending_notices,
                    "simulation.logDir": self.simulation.logDir,
                    "fundamental_price": self.fundamental_price
                },
                self.simulation_state_file + ".tmp",
            )
            if os.path.exists(self.simulation_state_file):
                os.remove(self.simulation_state_file)
            os.rename(self.simulation_state_file + ".tmp", self.simulation_state_file)
            bt.logging.success(f"Simulation state saved to {self.simulation_state_file} ({time.time()-start:.4f}s)")
            bt.logging.info("Saving validator state...")
            start = time.time()
            # Save the state of the validator to file.
            with open(self.validator_state_file + ".tmp", 'wb') as file:
                packed_data = msgpack.packb(
                    {
                        "step": self.step,
                        "simulation_timestamp": self.simulation_timestamp,
                        "hotkeys": self.hotkeys,
                        "scores": [score.item() for score in self.scores],
                        "activity_factors": self.activity_factors,
                        "inventory_history": self.inventory_history,
                        "sharpe_values": self.sharpe_values,
                        "unnormalized_scores": self.unnormalized_scores,
                        "trade_volumes" : self.trade_volumes,
                        "deregistered_uids" : self.deregistered_uids
                    }, use_bin_type=True
                )
                file.write(packed_data)
            if os.path.exists(self.validator_state_file):
                os.remove(self.validator_state_file)
            os.rename(self.validator_state_file + ".tmp", self.validator_state_file)
            bt.logging.success(f"Validator state saved to {self.validator_state_file} ({time.time()-start:.4f}s)")
        except Exception as ex:
            if os.path.exists(self.simulation_state_file + ".tmp"):
                os.remove(self.simulation_state_file + ".tmp")
            if os.path.exists(self.validator_state_file + ".tmp"):
                os.remove(self.validator_state_file + ".tmp")
            self.pagerduty_alert(f"Failed to save state : {ex}", details={"trace" : traceback.format_exc()})
        finally:
            self.saving = False

    def save_state(self) -> None:
        if not self.saving:
            Thread(target=self._save_state, args=(), daemon=True, name=f'save_{self.step}').start()

    def load_state(self) -> None:
        """Loads the state of the validator from a file."""
        if not self.config.neuron.reset and os.path.exists(self.simulation_state_file):
            bt.logging.info(f"Loading simulation state variables from {self.simulation_state_file}...")
            simulation_state = torch.load(self.simulation_state_file, weights_only=False)
            self.start_time = simulation_state["start_time"]
            self.start_timestamp = simulation_state["start_timestamp"]
            self.step_rates = simulation_state["step_rates"]
            self.pending_notices = simulation_state["pending_notices"]
            self.initial_balances = simulation_state["initial_balances"] if 'initial_balances' in simulation_state else {uid : {bookId : {'BASE' : None, 'QUOTE' : None, 'WEALTH' : None} for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
            for uid, initial_balances in self.initial_balances.items():
                if not 'WEALTH' in initial_balances[0]:
                    self.initial_balances[uid] = {bookId : initial_balance | {'WEALTH' : self.simulation.miner_wealth} for bookId, initial_balance in initial_balances.items()}
            self.recent_trades = simulation_state["recent_trades"]
            self.recent_miner_trades = simulation_state["recent_miner_trades"] if "recent_miner_trades" in simulation_state else {uid : {bookId : [] for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
            self.simulation.logDir = simulation_state["simulation.logDir"]
            self.fundamental_price = simulation_state["fundamental_price"]
            bt.logging.success(f"Loaded simulation state.")
        else:
            # If no state exists or the neuron.reset flag is set, re-initialize the simulation state
            if self.config.neuron.reset and os.path.exists(self.simulation_state_file):
                bt.logging.warning(f"`neuron.reset is True, ignoring previous state info at {self.simulation_state_file}.")
            else:
                bt.logging.info(f"No previous state information at {self.simulation_state_file}, initializing new simulation state.")
            self.pending_notices = {uid : [] for uid in range(self.subnet_info.max_uids)}
            self.initial_balances = {uid : {bookId : {'BASE' : None, 'QUOTE' : None, 'WEALTH' : None} for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
            self.recent_trades = {bookId : [] for bookId in range(self.simulation.book_count)}
            self.recent_miner_trades = {uid : {bookId : [] for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
            self.fundamental_price = {bookId : None for bookId in range(self.simulation.book_count)}

        if os.path.exists(self.validator_state_file.replace('.mp', '.pt')):
            bt.logging.info("Pytorch validator state file exists - converting to msgpack...")
            pt_validator_state = torch.load(self.validator_state_file.replace('.mp', '.pt'), weights_only=False)
            pt_validator_state["scores"] = [score.item() for score in pt_validator_state['scores']]
            with open(self.validator_state_file, 'wb') as file:
                packed_data = msgpack.packb(
                    pt_validator_state, use_bin_type=True
                )
                file.write(packed_data)
            os.rename(self.validator_state_file.replace('.mp', '.pt'), self.validator_state_file.replace('.mp', '.pt') + ".bak")
            bt.logging.info(f"Pytorch validator state file converted to msgpack at {self.validator_state_file}")

        if not self.config.neuron.reset and os.path.exists(self.validator_state_file):
            bt.logging.info(f"Loading validator state variables from {self.validator_state_file}...")
            with open(self.validator_state_file, 'rb') as file:
                byte_data = file.read()
            validator_state = msgpack.unpackb(byte_data, use_list=False, strict_map_key=False)
            self.step = validator_state["step"]
            self.simulation_timestamp = validator_state["simulation_timestamp"] if "simulation_timestamp" in validator_state else 0
            self.hotkeys = validator_state["hotkeys"]
            self.deregistered_uids = list(validator_state["deregistered_uids"]) if "deregistered_uids" in validator_state else []
            self.scores = torch.tensor(validator_state["scores"])
            self.activity_factors = validator_state["activity_factors"] if "activity_factors" in validator_state else {uid : {bookId : 0.0 for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
            if isinstance(self.activity_factors[0], float):
                self.activity_factors = {uid : {bookId : self.activity_factors[uid] for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
            self.inventory_history = validator_state["inventory_history"] if "inventory_history" in validator_state else {uid : {} for uid in range(self.subnet_info.max_uids)}
            for uid in self.inventory_history:
                for timestamp in self.inventory_history[uid]:
                    if len(self.inventory_history[uid][timestamp]) < self.simulation.book_count:
                        for bookId in range(len(self.inventory_history[uid][timestamp]),self.simulation.book_count):
                            self.inventory_history[uid][timestamp][bookId] = 0.0
                    if len(self.inventory_history[uid][timestamp]) > self.simulation.book_count:
                        self.inventory_history[uid][timestamp] = {k : v for k, v in self.inventory_history[uid][timestamp].items() if k < self.simulation.book_count}
            self.sharpe_values = validator_state["sharpe_values"]
            for uid in self.sharpe_values:
                if len(self.sharpe_values[uid]['books']) < self.simulation.book_count:
                    for bookId in range(len(self.sharpe_values[uid]['books']),self.simulation.book_count):
                        self.sharpe_values[uid]['books'][bookId] = 0.0
                if len(self.sharpe_values[uid]['books']) > self.simulation.book_count:
                    self.sharpe_values[uid]['books'] = {k : v for k, v in self.sharpe_values[uid]['books'].items() if k < self.simulation.book_count}
            self.unnormalized_scores = validator_state["unnormalized_scores"]
            self.trade_volumes = validator_state["trade_volumes"] if "trade_volumes" in validator_state else {uid : {bookId : {'total' : {}, 'maker' : {}, 'taker' : {}, 'self' : {}} for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
            reorg = False
            for uid in self.trade_volumes:
                for bookId in self.trade_volumes[uid]:
                    if not 'total' in self.trade_volumes[uid][bookId]:
                        if not reorg:
                            bt.logging.info(f"Optimizing miner volume history structures...")
                            reorg = True
                        volumes = {'total' : {}, 'maker' : {}, 'taker' : {}, 'self' : {}}
                        for time, role_volume in self.trade_volumes[uid][bookId].items():
                            sampled_time = math.ceil(time / self.config.scoring.activity.trade_volume_sampling_interval) * self.config.scoring.activity.trade_volume_sampling_interval
                            for role, volume in role_volume.items():
                                if not sampled_time in volumes[role]:
                                    volumes[role][sampled_time] = 0.0
                                volumes[role][sampled_time] += volume
                        self.trade_volumes[uid][bookId] = {role : {time : round(volumes[role][time], self.simulation.volumeDecimals) for time in volumes[role]} for role in volumes}
                if len(self.trade_volumes[uid]) < self.simulation.book_count:
                    for bookId in range(len(self.trade_volumes[uid]),self.simulation.book_count):
                        self.trade_volumes[uid][bookId] = {'total' : {}, 'maker' : {}, 'taker' : {}, 'self' : {}}
                if len(self.trade_volumes[uid]) > self.simulation.book_count:
                    self.trade_volumes[uid] = {k : v for k, v in self.trade_volumes[uid].items() if k < self.simulation.book_count}
                if len(self.activity_factors[uid]) < self.simulation.book_count:
                    for bookId in range(len(self.activity_factors[uid]),self.simulation.book_count):
                        self.activity_factors[uid][bookId] = 0.0
                if len(self.activity_factors[uid]) > self.simulation.book_count:
                    self.activity_factors[uid] = {k : v for k, v in self.activity_factors[uid].items() if k < self.simulation.book_count}
            if reorg:
                self._save_state()
            bt.logging.success(f"Loaded validator state.")
        else:
            # If no state exists or the neuron.reset flag is set, re-initialize the validator state
            if self.config.neuron.reset and os.path.exists(self.validator_state_file):
                bt.logging.warning(f"`neuron.reset is True, ignoring previous state info at {self.validator_state_file}.")
            else:
                bt.logging.info(f"No previous state information at {self.validator_state_file}, initializing new simulation state.")
            self.activity_factors = {uid : {bookId : 0.0 for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
            self.inventory_history = {uid : {} for uid in range(self.subnet_info.max_uids)}
            self.sharpe_values = {uid :
                {
                    'books' : {
                        bookId : 0.0 for bookId in range(self.simulation.book_count)
                    },
                    'total' : 0.0,
                    'average' : 0.0,
                    'median' : 0.0,
                    'normalized_average' : 0.0,
                    'normalized_total' : 0.0,
                    'normalized_median' : 0.0
                } for uid in range(self.subnet_info.max_uids)
            }
            self.unnormalized_scores = {uid : 0.0 for uid in range(self.subnet_info.max_uids)}
            self.trade_volumes = {uid : {bookId : {'total' : {}, 'maker' : {}, 'taker' : {}, 'self' : {}} for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}

    def load_simulation_config(self) -> None:
        """
        Reads elements from the config XML to populate the simulation config class object.
        """
        self.xml_config = ET.parse(self.config.simulation.xml_config).getroot()
        self.simulation = MarketSimulationConfig.from_xml(self.xml_config)
        self.validator_state_file = self.config.neuron.full_path + f"/validator.mp"
        self.simulation_state_file = self.config.neuron.full_path + f"/{self.simulation.label()}.pt"
        self.load_state()

    def __init__(self, config=None) -> None:
        """
        Initialize the intelligent markets simulation validator.
        """
        super(Validator, self).__init__(config=config)
        # Load the simulator config XML file data in order to make context and parameters accessible for reporting and output location.

        if not os.path.exists(self.config.simulation.xml_config):
            raise Exception(f"Simulator config does not exist at {self.config.simulation.xml_config}!")
        self.simulator_config_file = os.path.realpath(Path(self.config.simulation.xml_config))
        # Initialize subnet info and other basic validator/simulation properties
        self.subnet_info = self.subtensor.get_metagraph_info(self.config.netuid)
        self.last_state = None
        self.simulation_timestamp = 0
        self.reward_weights = {"sharpe" : 1.0}
        self.start_time = None
        self.start_timestamp = None
        self.last_state_time = None
        self.step_rates = []
        self.maintaining = False
        self.rewarding = False
        self.reporting = False
        self.saving = False
        self.compressing = False
        self.initial_balances_published = {uid : False for uid in range(self.subnet_info.max_uids)}

        self.load_simulation_config()

        # Add routes for methods receiving input from simulator
        self.router = APIRouter()
        self.router.add_api_route("/orderbook", self.orderbook, methods=["GET"])
        self.router.add_api_route("/account", self.account, methods=["GET"])

        self.repo_path = Path(os.path.dirname(os.path.realpath(__file__))).parent.parent.parent
        self.repo = Repo(self.repo_path)
        self.update_repo()

        self.miner_stats = {uid : {'requests' : 0, 'timeouts' : 0, 'failures' : 0, 'rejections' : 0, 'call_time' : []} for uid in range(self.subnet_info.max_uids)}
        init_metrics(self)
        publish_info(self)
        
    def load_fundamental(self):
        df_fp = pd.DataFrame()
        for block in range(self.simulation.block_count):
            block_file = os.path.join(self.simulation.logDir, f'fundamental.{block * self.simulation.books_per_block}-{self.simulation.books_per_block * (block + 1) - 1}.csv')
            block_fp = pd.read_csv(block_file) 
            block_fp.set_index('Timestamp', inplace=True)
            block_fp.columns = [int(col) for col in block_fp.columns]
            if block == 0:
                df_fp = block_fp
            else:
                df_fp = pd.merge(df_fp, block_fp, left_index=True, right_index=True, how='inner')
        self.fundamental_price = {bookId : df_fp[bookId] for bookId in range(self.simulation.book_count)}

    def onStart(self, timestamp, event : SimulationStartEvent) -> None:
        """
        Triggered when start of simulation event is published by simulator.
        Sets the simulation output directory and retrieves any fundamental price values already written.
        """
        bt.logging.info("-"*40)
        bt.logging.info("SIMULATION STARTED")
        self.load_simulation_config()
        self.trade_volumes = {
            uid : {
                bookId : {
                    role : {
                        prev_time - self.simulation_timestamp : volume for prev_time, volume in self.trade_volumes[uid][bookId][role].items() if prev_time - self.simulation_timestamp < self.simulation_timestamp
                    } for role in self.trade_volumes[uid][bookId]
                } for bookId in range(self.simulation.book_count)
            } for uid in range(self.subnet_info.max_uids)
        }
        self.start_time = time.time()
        self.simulation_timestamp = timestamp
        self.start_timestamp = self.simulation_timestamp
        self.last_state_time = None
        self.step_rates = []
        self.simulation.logDir = event.logDir
        self.compress_outputs(start=True)
        bt.logging.info(f"START TIME: {self.start_time}")
        bt.logging.info(f"TIMESTAMP : {self.start_timestamp}")
        bt.logging.info(f"OUT DIR   : {self.simulation.logDir}")
        bt.logging.info("-"*40)
        self.load_fundamental()
        self.initial_balances = {uid : {bookId : {'BASE' : None, 'QUOTE' : None, 'WEALTH' : self.simulation.miner_wealth} for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
        self.recent_trades = {bookId : [] for bookId in range(self.simulation.book_count)}
        self.recent_miner_trades = {uid : {bookId : [] for bookId in range(self.simulation.book_count)} for uid in range(self.subnet_info.max_uids)}
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
        Flags the UID as marked for balance reset.
        """
        self.deregistered_uids.append(uid)
        self.scores[uid] = 0.0
        bt.logging.debug(f"UID {uid} Deregistered - Scheduled for reset.")

    def process_resets(self, state : MarketSimulationStateUpdate) -> None:
        """
        Checks for and handles agent reset notices (due to deregistration).
        Zeroes scores and clears relevant internal variables.
        """        
        for notice in state.notices[self.uid]:
            if notice.type in ["RESPONSE_DISTRIBUTED_RESET_AGENT", "RDRA"] or notice.type in ["ERROR_RESPONSE_DISTRIBUTED_RESET_AGENT", "ERDRA"]:
                for reset in notice.resets:
                    if reset.success:
                        bt.logging.info(f"Agent {reset.agentId} Balances Reset! {reset}")
                        if reset.agentId in self.deregistered_uids:            
                            self.sharpe_values[reset.agentId] = {
                                'books' : {
                                    bookId : 0.0 for bookId in range(self.simulation.book_count)
                                },
                                'total' : 0.0,
                                'average' : 0.0,
                                'median' : 0.0,
                                'normalized_average' : 0.0,
                                'normalized_total' : 0.0,
                                'normalized_median' : 0.0
                            }
                            self.unnormalized_scores[reset.agentId] = 0.0
                            self.activity_factors[reset.agentId] = {bookId : 0.0 for bookId in range(self.simulation.book_count)}
                            self.inventory_history[reset.agentId] = {}
                            self.trade_volumes[reset.agentId] = {bookId : {'total' : {}, 'maker' : {}, 'taker' : {}, 'self' : {}} for bookId in range(self.simulation.book_count)}
                            self.initial_balances[reset.agentId] = {bookId : {'BASE' : None, 'QUOTE' : None, 'WEALTH' : None} for bookId in range(self.simulation.book_count)}
                            self.initial_balances_published[reset.agentId] = False
                            self.deregistered_uids.remove(reset.agentId)
                            self.miner_stats[reset.agentId] = {'requests' : 0, 'timeouts' : 0, 'failures' : 0, 'rejections' : 0, 'call_time' : []}
                    else:
                        self.pagerduty_alert(f"Failed to Reset Agent {reset.agentId} : {reset.message}")        

    def report(self) -> None:
        """
        Publish performance and state metrics.
        """
        if not self.config.reporting.disabled and not self.reporting:
            Thread(target=report, args=(self,), daemon=True, name=f'report_{self.step}').start()

    def _reward(self, state : MarketSimulationStateUpdate):
        # Calculate the rewards for the miner based on the latest simulation state.
        try:
            bt.logging.info(f"Updating Agent Scores at Step {self.step}...")
            self.rewarding = True
            start = time.time()
            rewards = get_rewards(self, state)
            bt.logging.debug(f"Agent Rewards Recalculated:\n{rewards}")
            # Update the miner scores.
            self.update_scores(rewards, self.metagraph.uids)
            bt.logging.info(f"Agent Scores Updated ({time.time()-start:.4f}s)")
            bt.logging.debug(f"{self.scores}")
        finally:
            self.rewarding = False

    def reward(self, state) -> None:
        """
        Update agent rewards and recalculate scores.
        """
        if not self.rewarding:
            Thread(target=self._reward, args=(state,), daemon=True, name=f'reward_{self.step}').start()

    async def orderbook(self, request : Request) -> dict:
        """
        The route method which receives and processes simulation state updates received from the simulator.
        """
        # Every 1H of simulation time, check if there are any changes to the validator - if updates exist, pull them and restart.
        if self.simulation_timestamp % 3600_000_000_000 == 0 and self.simulation_timestamp != 0:
            bt.logging.info("Checking for validator updates...")
            self.update_repo()
        bt.logging.debug("Received state update from simulator")
        global_start = time.time()
        start = time.time()
        body = await request.body()
        bt.logging.debug(f"Request body retrieved ({time.time()-start:.4f}s).")
        start = time.time()
        message = msgspec.json.decode(body)
        bt.logging.debug(f"Request body decoded ({time.time()-start:.4f}s).")
        start = time.time()
        state = MarketSimulationStateUpdate.from_json(message) # Populate synapse class from request data
        bt.logging.debug(f"Synapse populated ({time.time()-start:.4f}s).")
        
        # Update variables
        if not self.start_time:
            self.start_time = time.time()
            self.start_timestamp = state.timestamp
        if self.simulation.logDir != message['payload']['logDir']:
            bt.logging.info(f"Simulation log directory changed : {self.simulation.logDir} -> {message['payload']['logDir']}")
            self.simulation.logDir = message['payload']['logDir']
        self.simulation_timestamp = state.timestamp
        self.step_rates.append((state.timestamp - (self.last_state.timestamp if self.last_state else self.start_timestamp)) / (time.time() - (self.last_state_time if self.last_state_time else self.start_time)))
        self.last_state = state
        if self.simulation:
            state.config = self.simulation.model_copy()
            state.config.logDir = None
        self.step += 1
        
        if self.simulation_timestamp % self.simulation.log_window == self.simulation.publish_interval:
            self.compress_outputs()

        # Log received state data
        bt.logging.info(f"STATE UPDATE RECEIVED | VALIDATOR STEP : {self.step} | TIMESTAMP : {state.timestamp}")
        if self.config.logging.debug or self.config.logging.trace:
            debug_text = ''
            for bookId, book in state.books.items():
                debug_text += '-' * 50 + "\n"
                debug_text += f"BOOK {bookId}" + "\n"
                if book.bids and book.asks:
                    debug_text += ' | '.join([f"{level.quantity:.4f}@{level.price}" for level in reversed(book.bids[:5])]) + '||' + ' | '.join([f"{level.quantity:.4f}@{level.price}" for level in book.asks[:5]]) + "\n"
                else:
                    debug_text += "EMPTY" + "\n"
            bt.logging.debug("\n" + debug_text.strip("\n"))

        # Process deregistration notices
        self.process_resets(state)

        # Await reporting and state saving to complete before proceeding with next step
        while self.reporting or self.saving or self.rewarding or self.maintaining:
            bt.logging.info(f"Waiting for {'reporting' if self.reporting else ''}{', ' if self.reporting and self.saving else ''}{'state saving' if self.saving else ''}{', ' if (self.reporting or self.saving) and self.rewarding else ''}{'rewarding' if self.rewarding else ''}{', ' if (self.reporting or self.saving or self.rewarding) and self.maintaining else ''}{'maintaining' if self.maintaining else ''} to complete...")
            time.sleep(0.5)

        # Calculate latest rewards and update miner scores
        self.maintain()
        self.reward(state)
        # Forward state synapse to miners, populate response data to simulator object and serialize for returning to simulator.
        response = SimulatorResponseBatch(await forward(self, state)).serialize()
        
        # Log response data, start state serialization and reporting threads, and return miner instructions to the simulator
        if len(response['responses']) > 0:
            bt.logging.trace(f"RESPONSE : {response}")
        bt.logging.info(f"RATE : {(self.step_rates[-1] if self.step_rates != [] else 0) / 1e9:.2f} STEPS/s | AVG : {(sum(self.step_rates) / len(self.step_rates) / 1e9 if self.step_rates != [] else 0):.2f}  STEPS/s")
        self.step_rates = self.step_rates[-10000:]
        self.last_state_time = time.time()
        self.save_state()
        self.report()
        for notice in state.notices[0]:
            if notice.type in ['EVENT_SIMULATION_END', 'ESE']:
                self.onEnd()
        bt.logging.info(f"State update processed ({time.time()-global_start}s)")
        return response

    async def account(self, request : Request) -> None:
        """
        The route method which receives event notification messages from the simulator.
        This method is currently used only to enable the simulation start message to be immediately propagated to the validator.
        Other events are instead recorded to the simulation state object.
        """
        body = await request.body()
        batch = msgspec.json.decode(body)
        bt.logging.info(f"NOTICE : {batch}")
        notices = []
        for message in batch['messages']:
            if message['type'] == 'EVENT_SIMULATION_START':
                self.onStart(message['timestamp'], FinanceEventNotification.from_json(message).event)
            elif message['type'] == 'EVENT_SIMULATION_END':
                self.onEnd()
            else:
                notice = FinanceEventNotification.from_json(message)
                if not notice:
                    bt.logging.error(f"Unrecognized notification : {message}")
                else:
                    notices.append(notice.event)
        await notify(self, notices) # This method forwards the event notifications to the related miners.

# The main method which runs the validator
if __name__ == "__main__":
    from taos.im.validator.update import check_repo, update_validator, check_simulator, rebuild_simulator, restart_simulator
    from taos.im.validator.forward import forward, notify
    from taos.im.validator.report import report, publish_info, init_metrics
    from taos.im.validator.reward import get_rewards
    if float(platform.freedesktop_os_release()['VERSION_ID']) < 22.04:
        raise Exception(f"taos validator requires Ubuntu >= 22.04!")
    # Initialize FastAPI client and attach validator router
    app = FastAPI()
    validator = Validator()
    app.include_router(validator.router)
    # Start simulator price seeding data process in new thread
    Thread(target=validator.seed, daemon=True, name='Seed').start()
    # Run the validator as a FastAPI client via uvicorn on the configured port
    uvicorn.run(app, port=validator.config.port)
