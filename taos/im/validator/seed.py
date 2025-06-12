# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import os
import json
import time
import pandas as pd
import bittensor as bt

from binance.websocket.spot.websocket_stream import SpotWebsocketStreamClient
from taos.im.utils.coinbase import CoinbaseClient
from coinbase.websocket import WSClientConnectionClosedException,WSClientException

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
            
            self.external_count = 0
            self.sampled_external_count = 0
            self.external_filename = None
            self.sampled_external_filename = None
            last_external_count = self.external_count
            self.last_external = None
            self.last_sampled_external = None
            self.pending_external_data = ''
            external_update_counter = 0
            self.seed_exchange = 'coinbase'

            def binance_seed(_, message : str) -> None:
                try:
                    # bt.logging.trace(f"BI SEED {self.seed_count} : {message}")
                    message_dict = json.loads(message)
                    if 'e' in message_dict and message_dict['e'] == 'trade':
                        trade = {
                            "product_id" : message_dict['s'].lower(),
                            "price" : float(message_dict['p']),
                            "time" : pd.to_datetime(message_dict['T']*1000000).strftime('%Y-%m-%dT%H:%M:%S.%fZ'),
                            "timestamp" : message_dict['T']
                        }
                        match trade['product_id']:
                            case self.config.simulation.seeding.fundamental.symbol.binance:
                                record_seed(trade['price'])
                            case self.config.simulation.seeding.external.symbol.binance:
                                record_external(trade)
                except Exception as ex:
                    bt.logging.error(f"Exception getting Binance seed value : Message={message} | Error={ex}")

            def coinbase_seed(message : str) -> None:
                try:
                    # bt.logging.trace(f"CB SEED {self.seed_count} : {message}")
                    message_dict = json.loads(message)
                    if 'channel' in message_dict and message_dict['channel'] == 'market_trades':
                        for event in message_dict['events']:
                            if event['type'] == 'update' and 'trades' in event:
                                trade = {
                                    "product_id" : event['trades'][0]['product_id'],
                                    "price" : float(event['trades'][0]['price']),
                                    "time" : event['trades'][0]['time']
                                }
                                match trade['product_id']:
                                    case self.config.simulation.seeding.fundamental.symbol.coinbase:
                                        seed = float(trade['price'])
                                        record_seed(seed)
                                    case self.config.simulation.seeding.external.symbol.coinbase:
                                        trade['timestamp'] = pd.Timestamp(trade['time']).timestamp()
                                        record_external(trade)
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
                    
            def record_external(trade : dict) -> None:
                try:
                    if not self.last_external or self.last_external != trade:
                        self.external_count += 1
                        if not self.simulation.logDir:
                            self.pending_external_data += f"{self.external_count},{trade['price']},{trade['time']}\n"
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
                                            pass
                                        self.external_count = self.external_count + int(line.split(',')[0])
                                self.external_file = open(self.external_filename,'a')
                                self.external_file.write(self.pending_external_data)
                                self.pending_external_data = ''
                                
                                self.sampled_external_filename = os.path.join(self.simulation.logDir,"external_seed_sampled.csv")
                                if os.path.exists(self.sampled_external_filename) and os.stat(self.sampled_external_filename).st_size > 0:
                                    with open(self.sampled_external_filename) as f:
                                        for line in f:
                                            pass
                                        self.sampled_external_count = self.sampled_external_count + int(line.split(',')[0])
                                self.sampled_external_file = open(self.sampled_external_filename,'a')
                            self.external_file.write(f"{self.external_count},{trade['price']},{trade['time']}\n")
                            self.external_file.flush()
                            sampling_period = self.config.simulation.seeding.external.sampling_seconds
                            if self.last_external and ((not self.last_sampled_external and pd.to_datetime(self.last_external['time']).minute != pd.to_datetime(trade['time']).minute) or (self.last_sampled_external and self.last_sampled_external['timestamp'] < trade['timestamp'] - sampling_period)):
                                self.sampled_external_file.write(f"{self.sampled_external_count},{self.last_external['price']}\n")
                                self.sampled_external_file.flush()
                                self.last_sampled_external = self.last_external
                                self.sampled_external_count += 1
                            self.last_external = trade
                            
                except Exception as ex:
                    bt.logging.error(f"Exception in external price handling : trade={trade} | Error={ex}")

            def connect() -> None:
                attempts = 0
                while True:
                    try:
                        attempts += 1
                        self.seed_exchange='coinbase'
                        self.seed_client = CoinbaseClient(on_message=coinbase_seed)
                        bt.logging.info("Attempting to connect to Coinbase Trades Stream...")
                        self.seed_client.open()
                        self.seed_client.subscribe(product_ids=[self.config.simulation.seeding.fundamental.symbol.coinbase, self.config.simulation.seeding.external.symbol.coinbase], channels=["market_trades"])
                        bt.logging.success(f"Subscribed to Coinbase Trades Stream! [{self.config.simulation.seeding.fundamental.symbol.coinbase},{self.config.simulation.seeding.external.symbol.coinbase}]")
                        break
                    except Exception as ex:
                        bt.logging.warning(f"Unable to connect to Coinbase Trades Stream! {ex}. Trying Binance.")
                        try:
                            self.seed_exchange='binance'
                            self.seed_client = SpotWebsocketStreamClient(on_message=binance_seed)
                            self.seed_client.trade(symbol=self.config.simulation.seeding.fundamental.symbol.binance)
                            self.seed_client.trade(symbol=self.config.simulation.seeding.external.symbol.binance)
                            bt.logging.success(f"Subscribed to Binance Trades Stream! [{self.config.simulation.seeding.fundamental.symbol.binance},{self.config.simulation.seeding.external.symbol.binance}]")
                            break
                        except Exception as ex:
                            bt.logging.error(f"Unable to connect to Binance Trades Stream : {ex}.")
                            self.pagerduty_alert(f"Failed connecting to fundamental price seed stream (Attempt {attempts})")

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
                    reconnect = False
                    if self.last_seed:
                        self.seed_file.flush()
                        if last_count == self.seed_count:
                            bt.logging.warning(f"No new seed in last 10 seconds!  Restarting connection.")
                            if self.seed_exchange=='coinbase' and self.seed_client._is_websocket_open():
                                self.seed_client.close()
                            reconnect = True
                        last_count = self.seed_count
                    if self.last_external:
                        self.external_file.flush()
                        if last_external_count == self.external_count:
                            external_update_counter += 1
                            if external_update_counter >= 6: 
                                bt.logging.warning(f"No new external price in last 60 seconds!  Restarting connection.")
                                if self.seed_exchange=='coinbase' and self.seed_client._is_websocket_open():
                                    self.seed_client.close()
                                reconnect = True
                                external_update_counter = 0
                        else:
                            external_update_counter = 0
                        last_external_count = self.external_count
                    if reconnect:
                        connect()
                except Exception as ex:
                    bt.logging.error(f"Exception in seed loop : {ex}")
        except Exception as ex:
            self.pagerduty_alert(f"Failure in seeding process : {ex}", details={"traceback" : traceback.format_exc()})