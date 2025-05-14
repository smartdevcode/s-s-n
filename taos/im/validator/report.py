# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import os
import traceback
import time
import bittensor as bt
import pandas as pd

from taos.im.neurons.validator import Validator
from taos.im.protocol.models import TradeInfo

from taos.common.utils.prometheus import prometheus
from prometheus_client import Counter, Gauge, Info

def init_metrics(self : Validator) -> None:
    """
    Set up prometheus metric objects.

    Args:
        self (taos.im.neurons.validator.Validator): The intelligent markets simulation validator.
    Returns:
        None
    """
    prometheus (
        config = self.config,
        port = self.config.prometheus.port,
        level = None if not self.subtensor.chain_endpoint == "wss://test.finney.opentensor.ai:443" else "OFF"
    )
    self.prometheus_counters = Counter('counters', 'Counter summaries for the running validator.', ['wallet', 'netuid', 'timestamp', 'counter_name'])
    self.prometheus_simulation_gauges = Gauge('simulation_gauges', 'Gauge summaries for global simulation metrics.', ['wallet', 'netuid', 'simulation_gauge_name'])
    self.prometheus_validator_gauges = Gauge('validator_gauges', 'Gauge summaries for validator-related metrics.', ['wallet', 'netuid', 'validator_gauge_name'])
    self.prometheus_miner_gauges = Gauge('miner_gauges', 'Gauge summaries for miner-related metrics.', ['wallet', 'netuid', 'agent_id', 'miner_gauge_name'])
    self.prometheus_book_gauges = Gauge('book_gauges', 'Gauge summaries for book-related metrics.', ['wallet', 'netuid', 'book_id', 'level', 'book_gauge_name'])
    self.prometheus_agent_gauges = Gauge('agent_gauges', 'Gauge summaries for agent-related metrics.', ['wallet', 'netuid', 'book_id', 'agent_id', 'agent_gauge_name'])

    self.prometheus_trades = Gauge('trades', 'Gauge summaries for trade metrics.', ['wallet', 'netuid', 'timestamp', 'timestamp_str', 'book_id', 'agent_id', 'trade_id', 'aggressing_order_id', 'aggressing_agent_id', 'resting_order_id', 'resting_agent_id', 'price', 'volume', 'side', 'trade_gauge_name'])
    self.prometheus_miner_trades = Gauge('miner_trades', 'Gauge summaries for agent trade metrics.', ['wallet', 'netuid', 'timestamp', 'timestamp_str', 'book_id', 'uid', 'role', 'price', 'volume', 'side', 'miner_trade_gauge_name'])
    self.prometheus_books = Gauge('books', 'Gauge summaries for book snapshot metrics.', [
        'wallet', 'netuid', 'timestamp', 'timestamp_str', 'book_id',
        'bid_5', 'bid_vol_5', 'bid_4', 'bid_vol_4', 'bid_3', 'bid_vol_3', 'bid_2', 'bid_vol_2', 'bid_1', 'bid_vol_1',
        'ask_5', 'ask_vol_5', 'ask_4', 'ask_vol_4', 'ask_3', 'ask_vol_3', 'ask_2', 'ask_vol_2', 'ask_1', 'ask_vol_1',
        'book_gauge_name'
    ])
    self.prometheus_miners = Gauge('miners', 'Gauge summaries for miner metrics.', [
        'wallet', 'netuid', 'timestamp', 'timestamp_str', 'agent_id',
        'placement', 'base_balance', 'quote_balance', 'inventory_value', 'inventory_value_change', 'pnl', 'pnl_change', 
        'min_daily_volume','activity_factor', 'sharpe', 'unnormalized_score', 'score',
        'miner_gauge_name'
    ])
    self.prometheus_info = Info('neuron_info', "Info summaries for the running validator.", ['wallet', 'netuid'])

def publish_info(self : Validator) -> None:
    """
    Publishes static simulation and validator information metrics

    Args:
        self (taos.im.neurons.validator.Validator): The intelligent markets simulation validator.
    Returns:
        None
    """
    prometheus_info = {
        'uid': str(self.metagraph.hotkeys.index( self.wallet.hotkey.ss58_address )) if self.wallet.hotkey.ss58_address in self.metagraph.hotkeys else -1,
        'network': self.config.subtensor.network,
        'coldkey': str(self.wallet.coldkeypub.ss58_address),
        'coldkey_name': self.config.wallet.name,
        'hotkey': str(self.wallet.hotkey.ss58_address),
        'name': self.config.wallet.hotkey
    } | { f"simulation_{name}" : str(value) for name, value in self.simulation.model_dump().items() if name != 'logDir'}
    self.prometheus_info.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid ).info (prometheus_info)
    
def duration_from_timestamp(timestamp : int) -> str:
    seconds, nanoseconds = divmod(timestamp, 1_000_000_000)
    minutes, seconds = divmod(seconds, 60)
    hours, minutes = divmod(minutes, 60)
    days, hours = divmod(hours, 24)
    return (f"{days}d " if days > 0 else "") + f"{hours:02}:{minutes:02}:{seconds:02}.{nanoseconds:09d}"

def report(self : Validator) -> None:
    """
    Calculates and publishes metrics related to simulation state, validator and agent performance.

    Args:
        self (taos.im.neurons.validator.Validator): The intelligent markets simulation validator.
    Returns:
        None
    """
    try:
        self.reporting = True
        report_step = self.step
        bt.logging.debug(f"Publishing Metrics for Step {self.step}...")
        report_start = time.time()
        bt.logging.debug(f"Publishing simulation metrics...")
        start = time.time()
        self.prometheus_simulation_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, simulation_gauge_name="timestamp").set( self.simulation_timestamp )
        self.prometheus_simulation_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, simulation_gauge_name="step_rate").set( sum(self.step_rates) / len(self.step_rates) if len(self.step_rates) > 0 else 0 )
        has_new_trades = False
        has_new_miner_trades = False
        self.prometheus_validator_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, validator_gauge_name="uid").set( self.uid )
        self.prometheus_validator_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, validator_gauge_name="stake").set( self.metagraph.stake[self.uid] )
        self.prometheus_validator_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, validator_gauge_name="validator_trust").set( self.metagraph.validator_trust[self.uid] )
        self.prometheus_validator_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, validator_gauge_name="dividends").set( self.metagraph.dividends[self.uid] )
        self.prometheus_validator_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, validator_gauge_name="emission").set( self.metagraph.emission[self.uid] )
        self.prometheus_validator_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, validator_gauge_name="last_update").set( self.current_block - self.metagraph.last_update[self.uid] )
        self.prometheus_validator_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, validator_gauge_name="active").set( self.metagraph.active[self.uid] )
        self.prometheus_books.clear()
        bt.logging.debug(f"Simulation metrics published ({time.time()-start}s).")
        if self.simulation.logDir:
            bt.logging.debug(f"Retrieving fundamental prices...")
            start = time.time()
            df_fp = pd.read_csv(os.path.join(self.simulation.logDir,'fundamental.csv'))
            df_fp.set_index('Timestamp', inplace=True)
            self.fundamental_price = {bookId : df_fp[str(bookId)] for bookId in range(self.simulation.book_count)}
            bt.logging.debug(f"Retrieved fundamental prices ({time.time()-start}s).")
        a=0
        bt.logging.debug(f"Publishing book metrics...")
        book_start = time.time()
        for bookId, book in self.last_state.books.items():
            if book.bids:
                start = time.time()
                bid_cumsum = 0
                for i, level in enumerate(book.bids):
                    self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=i, book_gauge_name="bid").set( level.price )
                    self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=i, book_gauge_name="bid_vol").set( level.quantity )
                    bid_cumsum += level.quantity
                    self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=i, book_gauge_name="bid_vol_sum").set( bid_cumsum )
                    if i == 20: break
            if book.asks:
                start = time.time()
                ask_cumsum = 0
                for i, level in enumerate(book.asks):
                    self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=i, book_gauge_name="ask").set( level.price )
                    self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=i, book_gauge_name="ask_vol").set( level.quantity )
                    ask_cumsum += level.quantity
                    self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=i, book_gauge_name="ask_vol_sum").set( ask_cumsum )
                    if i == 20: break
            bt.logging.debug(f"Book {bookId} levels metrics published ({time.time()-start}s).")
            if book.bids and book.asks:
                start = time.time()
                self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=0, book_gauge_name="mid").set( (book.bids[0].price + book.asks[0].price) / 2 )
                def get_price(side, idx):
                    if side == 'bid':
                        return book.bids[idx].price if len(book.bids) > idx else 0
                    if side == 'ask':
                        return book.asks[idx].price if len(book.asks) > idx else 0
                def get_vol(side, idx):
                    if side == 'bid':
                        return book.bids[idx].quantity if len(book.bids) > idx else 0
                    if side == 'ask':
                        return book.asks[idx].quantity if len(book.asks) > idx else 0
                self.prometheus_books.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, timestamp=self.simulation_timestamp, timestamp_str=duration_from_timestamp(self.simulation_timestamp), book_id=bookId,
                    bid_5=get_price('bid',4), bid_vol_5=get_vol('bid',4),bid_4=get_price('bid',3), bid_vol_4=get_vol('bid',3),bid_3=get_price('bid',2), bid_vol_3=get_vol('bid',2),bid_2=get_price('bid',1), bid_vol_2=get_vol('bid',1),bid_1=get_price('bid',0), bid_vol_1=get_vol('bid',0),
                    ask_5=get_price('ask',4), ask_vol_5=get_vol('ask',4),ask_4=get_price('ask',3), ask_vol_4=get_vol('ask',3),ask_3=get_price('ask',2), ask_vol_3=get_vol('ask',2),ask_2=get_price('ask',1), ask_vol_2=get_vol('ask',1),ask_1=get_price('ask',0), ask_vol_1=get_vol('ask',0),
                    book_gauge_name='books'
                ).set( 1.0 )
                bt.logging.debug(f"Book {bookId} aggregate metrics published ({time.time()-start}s).")
            if book.events:
                trades = [event for event in book.events if isinstance(event, TradeInfo)]
                if len(trades) > 0:
                    start = time.time()
                    last_trade = trades[-1]
                    if isinstance(self.fundamental_price[0],pd.Series):
                        self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=0, book_gauge_name="fundamental_price").set( self.fundamental_price[bookId].iloc[-1] )
                    self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=0, book_gauge_name="trade_price").set( last_trade.price )
                    self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=0, book_gauge_name="trade_volume").set( sum([trade.quantity for trade in trades]) )
                    self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=0, book_gauge_name="trade_buy_volume").set( sum([trade.quantity for trade in trades if trade.side == 0]) )
                    self.prometheus_book_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, level=0, book_gauge_name="trade_sell_volume").set( sum([trade.quantity for trade in trades if trade.side == 1]) )
                    self.recent_trades[bookId].extend(trades)
                    self.recent_trades[bookId] = self.recent_trades[bookId][-25:]
                    miner_trades = [trade for trade in trades if trade.maker_agent_id >=0 or trade.taker_agent_id >=0]
                    if len(miner_trades) > 0:                        
                        has_new_miner_trades = True
                        for trade in miner_trades:
                            if trade.maker_agent_id >= 0:
                                self.recent_miner_trades[trade.maker_agent_id][bookId].append(trade)
                            if trade.taker_agent_id >= 0:
                                self.recent_miner_trades[trade.taker_agent_id][bookId].append(trade)
                    has_new_trades = True
                bt.logging.debug(f"Book {bookId} events metrics published ({time.time()-start}s).")
            
        bt.logging.debug(f"Book metrics published ({time.time()-book_start}s).")
        if has_new_trades:
            bt.logging.debug(f"Publishing trade metrics...")
            start = time.time()
            self.prometheus_trades.clear()
            for bookId, trades in self.recent_trades.items():
                for trade in trades:
                    self.prometheus_trades.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, 
                                                timestamp=trade.timestamp, timestamp_str=duration_from_timestamp(trade.timestamp), 
                                                book_id=bookId, agent_id=trade.taker_agent_id,
                                                trade_id=trade.id, aggressing_order_id=trade.taker_id, resting_order_id=trade.maker_id,
                                                aggressing_agent_id=trade.taker_agent_id, resting_agent_id=trade.maker_agent_id,
                                                price=trade.price, volume=trade.quantity, side=trade.side, trade_gauge_name="trades").set( 1.0 )
            bt.logging.debug(f"Trade metrics published ({time.time()-start}s).")
            
        if has_new_miner_trades:
            bt.logging.debug(f"Publishing miner trade metrics...")
            self.prometheus_miner_trades.clear()
            for uid in self.recent_miner_trades:
                for bookId in self.recent_miner_trades[uid]:
                    self.recent_miner_trades[uid][bookId] = self.recent_miner_trades[uid][bookId][-5:]
                    for miner_trade in self.recent_miner_trades[uid][bookId]:
                        self.prometheus_miner_trades.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, 
                                timestamp=miner_trade.timestamp, timestamp_str=duration_from_timestamp(miner_trade.timestamp), book_id=bookId, uid=uid,
                                role="maker" if miner_trade.maker_agent_id == uid and miner_trade.taker_agent_id != uid else ("taker" if miner_trade.taker_agent_id == uid and miner_trade.maker_agent_id != uid else "both"),
                                price=miner_trade.price, volume=miner_trade.quantity, side=miner_trade.side, miner_trade_gauge_name="miner_trades").set( 1.0 )
            bt.logging.debug(f"Miner Trade metrics published ({time.time()-start}s).")

        if self.last_state.accounts:
            bt.logging.debug(f"Publishing accounts metrics...")
            daily_volumes = {agentId : {bookId : round(sum(self.trade_volumes[agentId][bookId].values()), self.simulation.volumeDecimals) for bookId in range(self.simulation.book_count)} for agentId in self.last_state.accounts.keys() }
            # start = time.time()
            # for agentId, accounts in self.last_state.accounts.items():
            #     if agentId < 0 or len(self.inventory_history[agentId]) < 3: continue
            #     start_inv = [i for i in list(self.inventory_history[agentId].values()) if len(i) > bookId][0]
            #     last_inv = list(self.inventory_history[agentId].values())[-1]
            #     sharpes = self.sharpe_values[agentId]
            #     activity_factors = {bookId : round(min(1.0, round(sum(self.trade_volumes[agentId][bookId].values()), self.simulation.volumeDecimals) / round(self.simulation.miner_wealth, self.simulation.volumeDecimals)),6) for bookId in accounts.keys()}
            #     for bookId, account in accounts.items():
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="base_balance_total").set( account.base_balance.total )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="base_balance_free").set( account.base_balance.free )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="base_balance_reserved").set( account.base_balance.reserved )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="quote_balance_total").set( account.quote_balance.total )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="quote_balance_free").set( account.quote_balance.free )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="quote_balance_reserved").set( account.quote_balance.reserved )
                    
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="inventory_value").set( last_inv[bookId] )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="pnl").set( last_inv[bookId] - start_inv[bookId] )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="daily_volume").set( daily_volumes[agentId][bookId] )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="activity_factor").set( activity_factors[bookId] )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="sharpe").set( sharpes['books'][bookId] )
            # bt.logging.debug(f"Agent book metrics published ({time.time()-start}s).")
            
            
            start = time.time()
            self.prometheus_miners.clear()
            # # neurons_lite call fails after first call, we cannot calculate network-wide miner placement until this is resolved
            # neurons = self.subtensor.neurons_lite(self.config.netuid)
            # network_scores = torch.tensor([n.pruning_score for n in neurons])
            # sorted_network_scores = network_scores.sort()
            total_inventory_history = {}
            pnl = {}
            placement = {}
            scores = self.scores.detach().clone()
            sorted_scores = scores.sort(descending=True)
            for agentId, accounts in self.last_state.accounts.items():
                if agentId < 0 or len(self.inventory_history[agentId]) < 3: continue
                total_inventory_history[agentId] = [sum(list(inventory_value.values())) for inventory_value in list(self.inventory_history[agentId].values())]
                pnl[agentId] = total_inventory_history[agentId][-1] - total_inventory_history[agentId][0]
                total_base_balance = round(sum([accounts[bookId].base_balance.total for bookId in self.last_state.books]), self.simulation.baseDecimals)
                total_quote_balance = round(sum([accounts[bookId].quote_balance.total for bookId in self.last_state.books]), self.simulation.baseDecimals)
                total_daily_volume = round(sum(daily_volumes[agentId].values()), self.simulation.volumeDecimals)
                average_daily_volume = round(total_daily_volume / len(daily_volumes[agentId]), self.simulation.volumeDecimals)
                min_daily_volume = min(daily_volumes[agentId].values())                
                # # neurons_lite call fails after first call, we cannot calculate network-wide miner placement until this is resolved
                # uids_at_score = (network_scores == neurons[agentId].pruning_score).nonzero().flatten().sort().values.flip(0)
                # min_place_for_score = (sorted_scores.values == neurons[agentId].pruning_score).nonzero().flatten()[0].item()
                # placement = min_place_for_score + (uids_at_score == agentId).nonzero().flatten().item()
                uids_at_score = (scores == scores[agentId]).nonzero().flatten().sort().values.flip(0)
                min_place_for_score = (sorted_scores.values == scores[agentId]).nonzero().flatten()[-1].item()
                placement = min_place_for_score - (uids_at_score == agentId).nonzero().flatten().item()
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="total_base_balance").set(total_base_balance)
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="total_quote_balance").set(total_quote_balance)
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="total_inventory_value").set(total_inventory_history[agentId][-1])
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="pnl").set(pnl[agentId])
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="total_daily_volume").set(total_daily_volume)
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="average_daily_volume").set(average_daily_volume)
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="min_daily_volume").set(min_daily_volume)
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="activity_factor").set(self.activity_factors[agentId])                
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="sharpe").set(self.sharpe_values[agentId]['total'])
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="unnormalized_score").set( self.unnormalized_scores[agentId] )
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="score").set( scores[agentId].item() )
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,miner_gauge_name="placement").set(placement)
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId, miner_gauge_name="trust").set(self.metagraph.trust[agentId] if len(self.metagraph.trust) > agentId else 0.0)
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId, miner_gauge_name="consensus").set(self.metagraph.consensus[agentId] if len(self.metagraph.consensus) > agentId else 0.0)
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId, miner_gauge_name="incentive").set(self.metagraph.incentive[agentId] if len(self.metagraph.incentive) > agentId else 0.0)
                self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId, miner_gauge_name="emission").set(self.metagraph.emission[agentId] if len(self.metagraph.emission) > agentId else 0.0)
                if self.simulation_timestamp % (self.simulation.publish_interval * 100) == 0:
                    self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId, miner_gauge_name="requests").set( self.miner_stats[agentId]['requests'] )
                    self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId, miner_gauge_name="success").set( self.miner_stats[agentId]['requests'] - self.miner_stats[agentId]['failures'] - self.miner_stats[agentId]['timeouts'] - self.miner_stats[agentId]['rejections'] )
                    self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId, miner_gauge_name="failures").set( self.miner_stats[agentId]['failures'] )
                    self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId, miner_gauge_name="timeouts").set( self.miner_stats[agentId]['timeouts'] )
                    self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId, miner_gauge_name="rejections").set( self.miner_stats[agentId]['rejections'] )
                    self.prometheus_miner_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId, miner_gauge_name="call_time").set( sum(self.miner_stats[agentId]['call_time']) / len(self.miner_stats[agentId]['call_time']) if len(self.miner_stats[agentId]['call_time']) > 0 else 0 )
                    self.miner_stats[agentId] = {'requests' : 0, 'timeouts' : 0, 'failures' : 0, 'rejections' : 0, 'call_time' : []}
                self.prometheus_miners.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, agent_id=agentId,
                    timestamp=self.simulation_timestamp, timestamp_str=duration_from_timestamp(self.simulation_timestamp),
                    placement=placement, base_balance=total_base_balance, quote_balance=total_quote_balance,
                    inventory_value=total_inventory_history[agentId][-1], inventory_value_change=total_inventory_history[agentId][-1] - total_inventory_history[agentId][-2] if len(total_inventory_history[agentId]) > 1 else 0.0,
                    pnl=pnl[agentId], pnl_change=pnl[agentId] - (total_inventory_history[agentId][-2] - total_inventory_history[agentId][0]) if len(total_inventory_history[agentId]) > 1 else 0.0,
                    min_daily_volume=min_daily_volume, activity_factor=self.activity_factors[agentId], sharpe=self.sharpe_values[agentId]['total'], unnormalized_score=self.unnormalized_scores[agentId], score=scores[agentId].item(),
                    miner_gauge_name='miners'
                ).set( 1.0 )
            bt.logging.debug(f"Accounts metrics published ({time.time()-start}s).")
        bt.logging.info(f"Metrics Published for Step {report_step}  ({time.time()-report_start}s).")

    except Exception as ex:
        traceback.print_exc()
        bt.logging.error(f"Unable to publish metrics : {traceback.print_exc()}")
    finally:
        self.reporting = False