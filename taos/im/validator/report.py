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

    self.prometheus_trades = Gauge('trades', 'Gauge summaries for trade metrics.', ['wallet', 'netuid', 'timestamp', 'book_id', 'agent_id', 'trade_id', 'aggressing_order_id', 'aggressing_agent_id', 'resting_order_id', 'resting_agent_id', 'price', 'volume', 'side', 'trade_gauge_name'])
    self.prometheus_books = Gauge('books', 'Gauge summaries for book snapshot metrics.', [
        'wallet', 'netuid', 'timestamp', 'book_id',
        'bid_5', 'bid_vol_5', 'bid_4', 'bid_vol_4', 'bid_3', 'bid_vol_3', 'bid_2', 'bid_vol_2', 'bid_1', 'bid_vol_1',
        'ask_5', 'ask_vol_5', 'ask_4', 'ask_vol_4', 'ask_3', 'ask_vol_3', 'ask_2', 'ask_vol_2', 'ask_1', 'ask_vol_1',
        'book_gauge_name'
    ])
    self.prometheus_miners = Gauge('miners', 'Gauge summaries for miner metrics.', [
        'wallet', 'netuid', 'timestamp', 'agent_id',
        'placement', 'base_balance', 'quote_balance', 'inventory_value', 'inventory_value_change', 'pnl', 'pnl_change', 'sharpe', 'unnormalized_score', 'score',
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
    self.prometheus_info.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid ).info ({
        'uid': str(self.metagraph.hotkeys.index( self.wallet.hotkey.ss58_address )) if self.wallet.hotkey.ss58_address in self.metagraph.hotkeys else -1,
        'network': self.config.subtensor.network,
        'coldkey': str(self.wallet.coldkeypub.ss58_address),
        'coldkey_name': self.config.wallet.name,
        'hotkey': str(self.wallet.hotkey.ss58_address),
        'name': self.config.wallet.hotkey,
        'simulation_duration' : str(self.simulation.duration),
        'simulation_grace_period' : str(self.simulation.grace_period),
        'simulation_publish_interval' : str(self.simulation.publish_interval),

        'simulation_book_count' : str(self.simulation.book_count),
        'simulation_book_levels' : str(self.simulation.book_levels),

        'simulation_start_base_balance' : str(self.simulation.start_base_balance),
        'simulation_start_quote_balance' : str(self.simulation.start_quote_balance),
        'simulation_sta_baseDecimals' : str(self.simulation.baseDecimals),
        'simulation_sta_quoteDecimals' : str(self.simulation.quoteDecimals),
        'simulation_sta_priceDecimals' : str(self.simulation.priceDecimals),
        'simulation_sta_volumeDecimals' : str(self.simulation.volumeDecimals),

        'simulation_init_agent_count' : str(self.simulation.init_agent_count),
        'simulation_init_price' : str(self.simulation.init_price),

        'fp_GBM_mu' : str(self.simulation.fp_GBM_mu),
        'fp_GBM_sigma' : str(self.simulation.fp_GBM_sigma),
        'fp_GBM_lambda_jump' : str(self.simulation.fp_GBM_lambda_jump),
        'fp_GBM_mu_jump' : str(self.simulation.fp_GBM_mu_jump),
        'fp_GBM_sigma_jump' : str(self.simulation.fp_GBM_sigma_jump),
        'fp_GBM_flag_jump' : str(self.simulation.fp_GBM_flag_jump),
        'fp_GBM_seed' : str(self.simulation.fp_GBM_seed),

        'simulation_sta_noise_agent_weight' : str(self.simulation.sta_noise_agent_weight),
        'simulation_sta_chartist_agent_weight' : str(self.simulation.sta_chartist_agent_weight),
        'simulation_sta_fundamentalist_agent_weight' : str(self.simulation.sta_fundamentalist_agent_weight),
        'simulation_sta_tau' : str(self.simulation.sta_tau),
        'simulation_sta_sigmaEps' : str(self.simulation.sta_sigmaEps),
        'simulation_sta_r_aversion' : str(self.simulation.sta_r_aversion),

        'hft_agent_count' : str(self.simulation.hft_agent_count),
        'hft_tau' : str(self.simulation.hft_tau),
        'hft_delta' : str(self.simulation.hft_delta),
        'hft_psiHFT' : str(self.simulation.hft_psiHFT),
        'hft_gHFT' : str(self.simulation.hft_gHFT),
    })

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
        self.prometheus_simulation_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, simulation_gauge_name="step_rate").set( sum(self.step_rates) / len(self.step_rates) )
        has_new_trades = False
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
                self.prometheus_books.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, timestamp=self.simulation_timestamp, book_id=bookId,
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
                    has_new_trades = True
                bt.logging.debug(f"Book {bookId} events metrics published ({time.time()-start}s).")
            
        bt.logging.debug(f"Book metrics published ({time.time()-book_start}s).")
        if has_new_trades:
            bt.logging.debug(f"Publishing trade metrics...")
            start = time.time()
            self.prometheus_trades.clear()
            for bookId, trades in self.recent_trades.items():
                for trade in trades:
                    self.prometheus_trades.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, timestamp=trade.timestamp, book_id=bookId, agent_id=trade.taker_agent_id,
                                                trade_id=trade.id, aggressing_order_id=trade.taker_id, resting_order_id=trade.maker_id,
                                                aggressing_agent_id=trade.taker_agent_id, resting_agent_id=trade.maker_agent_id,
                                                price=trade.price, volume=trade.quantity, side=trade.side, trade_gauge_name="trades").set( 1.0 )
            bt.logging.debug(f"Trade metrics published ({time.time()-start}s).")

        if self.last_state.accounts:
            bt.logging.debug(f"Publishing accounts metrics...")
            # start = time.time()
            # for agentId, accounts in self.last_state.accounts.items():
            #     if agentId < 0 or len(self.inventory_history[agentId]) < 3: continue
            #     start_inv = [i for i in list(self.inventory_history[agentId].values()) if len(i) > bookId][0]
            #     last_inv = list(self.inventory_history[agentId].values())[-1]
            #     sharpes = self.sharpe_values[agentId]
            #     for bookId, account in accounts.items():
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="base_balance_total").set( account.base_balance.total )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="base_balance_free").set( account.base_balance.free )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="base_balance_reserved").set( account.base_balance.reserved )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="quote_balance_total").set( account.quote_balance.total )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="quote_balance_free").set( account.quote_balance.free )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="quote_balance_reserved").set( account.quote_balance.reserved )
                    
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="inventory_value").set( last_inv[bookId] )
            #         self.prometheus_agent_gauges.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, book_id=bookId, agent_id=agentId, agent_gauge_name="pnl").set( last_inv[bookId] - start_inv[bookId] )
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
                total_base_balance = sum([accounts[bookId].base_balance.total for bookId in self.last_state.books])
                total_quote_balance = sum([accounts[bookId].quote_balance.total for bookId in self.last_state.books])
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
                self.prometheus_miners.labels( wallet=self.wallet.hotkey.ss58_address, netuid=self.config.netuid, timestamp=self.simulation_timestamp, agent_id=agentId,
                    placement=placement, base_balance=total_base_balance, quote_balance=total_quote_balance,
                    inventory_value=total_inventory_history[agentId][-1], inventory_value_change=total_inventory_history[agentId][-1] - total_inventory_history[agentId][-2] if len(total_inventory_history[agentId]) > 1 else 0.0,
                    pnl=pnl[agentId], pnl_change=pnl[agentId] - (total_inventory_history[agentId][-2] - total_inventory_history[agentId][0]) if len(total_inventory_history[agentId]) > 1 else 0.0,
                    sharpe=self.sharpe_values[agentId]['total'], unnormalized_score=self.unnormalized_scores[agentId], score=scores[agentId].item(),
                    miner_gauge_name='miners'
                ).set( 1.0 )
            bt.logging.debug(f"Accounts metrics published ({time.time()-start}s).")
        bt.logging.info(f"Metrics Published for Step {report_step}  ({time.time()-report_start}s).")

    except Exception as ex:
        bt.logging.error(f"Unable to publish metrics : {traceback.print_exc()}")
    finally:
        self.reporting = False