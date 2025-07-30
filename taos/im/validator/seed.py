# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import os
import json
import traceback
import time
import pandas as pd
import bittensor as bt

from binance.websocket.spot.websocket_stream import SpotWebsocketStreamClient as BinanceClient
from taos.im.utils.coinbase import CoinbaseClient
from coinbase.websocket import WSClientConnectionClosedException,WSClientException

from taos.im.utils.streams import *

def seed(self) -> None:
        """
        Retrieve data for use as simulation fundamental price and external seed, and record to simulator-accessible location.
        This process is run in a separate thread parallel to the FastAPI server.
        """
        try:
            self.seed_count = 0
            self.seed_filename = None
            self.last_seed_count = self.seed_count
            self.last_seed = None
            self.pending_seed_data = ''
            
            self.external_count = 0
            self.sampled_external_count = 0
            self.external_filename = None
            self.sampled_external_filename = None
            self.last_external_count = self.external_count
            self.last_external = None
            self.last_sampled_external = None
            self.next_sampled_external = None
            self.next_external_sampling_time = None
            self.pending_external_data = ''
            self.seed_exchange = 'coinbase'
            
            def on_coinbase_trade(trade : dict):
                match trade['product_id']:
                    case self.config.simulation.seeding.fundamental.symbol.coinbase:
                        record_seed(trade)
                    case self.config.simulation.seeding.external.symbol.coinbase:
                        if self.next_external_sampling_time and trade['received'] <= self.next_external_sampling_time:
                            self.next_sampled_external = trade
                        record_external(trade)

            def on_binance_trade(trade : dict):
                match trade['product_id']:
                    case self.config.simulation.seeding.fundamental.symbol.binance:
                        record_seed(trade)
                    case self.config.simulation.seeding.external.symbol.binance:
                        record_external(trade)

            def record_seed(trade : dict) -> None:
                try:
                    seed = trade['price']
                    if not self.last_seed or self.last_seed['price'] != seed:
                        if not self.simulation.logDir:
                            self.seed_count += 1
                            self.pending_seed_data += f"{self.seed_count},{seed}\n"
                            if len(self.pending_seed_data.split("\n")) > 10000:
                                self.pending_seed_data = "\n".join(self.pending_seed_data.split("\n")[-10000:])
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
                                            self.seed_count += 1
                                self.seed_file = open(self.seed_filename,'a')
                                self.seed_file.write(self.pending_seed_data)
                                self.pending_seed_data = ''
                            self.seed_count += 1
                            self.seed_file.write(f"{self.seed_count},{seed}\n")
                            self.seed_file.flush()
                            self.last_seed = trade
                except Exception as ex:
                    bt.logging.error(f"Exception in seed handling : Seed={seed} | Error={ex}")
                    
            def record_external(trade : dict) -> None:
                try:
                    if not self.last_external or self.last_external != trade:
                        if not self.simulation.logDir:
                            self.external_count += 1
                            self.pending_external_data += f"{self.external_count},{trade['price']},{trade['time']}\n"
                            if len(self.pending_external_data.split("\n")) > 10000:
                                self.pending_external_data = "\n".join(self.pending_external_data.split("\n")[-10000:])
                        else:
                            if self.external_filename != os.path.join(self.simulation.logDir,"external_seed.csv"):
                                self.last_external = None
                                self.external_count = 0
                                self.pending_external_data = ''
                            if not self.last_external:
                                self.external_filename = os.path.join(self.simulation.logDir,"external_seed.csv")
                                if os.path.exists(self.external_filename) and os.stat(self.external_filename).st_size > 0:
                                    with open(self.external_filename) as f:
                                        for line in f:
                                            self.external_count += 1
                                self.external_file = open(self.external_filename,'a')
                                self.external_file.write(self.pending_external_data)
                                self.pending_external_data = ''
                                
                                self.sampled_external_filename = os.path.join(self.simulation.logDir,"external_seed_sampled.csv")
                                if os.path.exists(self.sampled_external_filename) and os.stat(self.sampled_external_filename).st_size > 0:
                                    with open(self.sampled_external_filename) as f:
                                        for line in f:
                                            self.sampled_external_count += 1
                                self.sampled_external_file = open(self.sampled_external_filename,'a')
                            self.external_count += 1
                            self.external_file.write(f"{self.external_count},{trade['price']},{trade['time']}\n")
                            self.external_file.flush()
                            self.last_external = trade
                            
                except Exception as ex:
                    bt.logging.error(f"Exception in external price handling : trade={trade} | Error={ex}")
                
            def connect() -> None:
                attempts = 0
                while True:
                    attempts += 1
                    self.seed_exchange='coinbase'
                    self.seed_client, ex = connect_coinbase([self.config.simulation.seeding.fundamental.symbol.coinbase, self.config.simulation.seeding.external.symbol.coinbase], on_coinbase_trade)
                    if not self.seed_client:
                        bt.logging.warning(f"Unable to connect to Coinbase Trades Stream! {ex}. Trying Binance.")
                        self.seed_exchange='binance'
                        self.seed_client, ex = connect_binance([self.config.simulation.seeding.fundamental.symbol.binance,self.config.simulation.seeding.external.symbol.binance], on_binance_trade)
                        if not self.seed_client:
                            bt.logging.error(f"Unable to connect to Binance Trades Stream : {ex}.")
                            self.pagerduty_alert(f"Failed connecting to seed streams (Attempt {attempts})")
                        else:
                            break
                    else:
                        break
            
            def check_seeds():
                reconnect = False
                if self.last_seed:
                    self.seed_file.flush()
                    if self.last_seed['received'] < time.time() - 10:
                        bt.logging.warning(f"No new seed in last 10 seconds!  Restarting connection.")
                        if self.seed_exchange=='coinbase' and self.seed_client._is_websocket_open():
                            self.seed_client.close()
                        reconnect = True
                        self.last_seed = None
                        self.seed_count = 0
                    self.last_seed_count = self.seed_count
                if self.last_external:
                    self.external_file.flush()
                    if self.last_external['received'] < time.time() - 120:
                        bt.logging.warning(f"No new external price in last 120 seconds!  Restarting connection.")
                        if self.seed_exchange=='coinbase' and self.seed_client._is_websocket_open():
                            self.seed_client.close()
                        reconnect = True
                        self.last_external = None
                        self.external_count = 0
                    sampling_period = self.config.simulation.seeding.external.sampling_seconds
                    current_time = time.time()
                    if not self.next_external_sampling_time or not self.next_sampled_external:
                        seconds_since_start_of_day = current_time % 86400
                        start_of_day = current_time - seconds_since_start_of_day
                        self.next_external_sampling_time = start_of_day + seconds_since_start_of_day + (sampling_period - (seconds_since_start_of_day % sampling_period))
                    if current_time >= self.next_external_sampling_time and self.next_sampled_external:
                        self.sampled_external_count += 1
                        self.next_sampled_external['received'] = self.next_external_sampling_time
                        self.sampled_external_file.write(f"{self.sampled_external_count},{self.next_sampled_external['price']}\n")
                        self.sampled_external_file.flush()
                        self.last_sampled_external = self.next_sampled_external
                        self.last_sampled_external['received'] = self.next_external_sampling_time
                        self.next_external_sampling_time = self.next_external_sampling_time + sampling_period
                return not reconnect

            connect()
            while True:
                try:
                    if self.seed_exchange=='coinbase':
                        maintain_coinbase(self.seed_client, connect, check_seeds, 1)
                    if self.seed_exchange=='binance':
                        maintain_binance(self.seed_client, connect, check_seeds, 1)
                except Exception as ex:
                    bt.logging.error(f"Exception in seed loop : {ex}")
        except Exception as ex:
            self.pagerduty_alert(f"Failure in seeding process : {ex}", details={"traceback" : traceback.format_exc()})