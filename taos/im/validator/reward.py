# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
# The MIT License (MIT)
# Copyright © 2023 Yuma Rao
# Copyright © 2025 Rayleigh Research

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

import torch
import math
import traceback
import random
import bittensor as bt
import numpy as np
from typing import Dict
from taos.im.neurons.validator import Validator
from taos.im.protocol import MarketSimulationStateUpdate, FinanceAgentResponse
from taos.im.protocol.models import Account, Book, TradeInfo
from taos.im.utils import normalize
from taos.im.utils.sharpe import sharpe, batch_sharpe

def get_inventory_value(account : Account, book : Book, method='midquote') -> float:
    """
    Calculates the instantaneous total value of an account's inventory using the specified method

    Args:
        account (taos.im.protocol.models.Account) : Object representing the state of the account to be evaluated
        book : Object representing the orderbook with which the account is associated
        method : String identifier of the method by which the value should be calculated; options are
            a) `best_bid` : Calculates base currency balance value using only the top level bid price
            b) `midquote` : Calculates base currency balance value using the midquote price `(bid + ask) / 2`
            c) `liquidation` : Calculates base currency balance value by evaluating the total amount received if base balance is sold immediately and in isolation into the current book

    Returns:
        float: Total inventory value of the account.
    """
    match method:
        case "best_bid":
            return account.own_quote + (book.bids[0].price if len(book.asks) > 0 and len(book.bids) > 0 else 0.0) * account.own_base
        case "midquote":
            return account.own_quote + ((book.asks[0].price + book.bids[0].price) / 2 if len(book.asks) > 0 and len(book.bids) > 0 else 0.0) * account.own_base
        case "liquidation":
            liq_value = 0
            to_liquidate = account.base_balance.total
            for bid in book.bids:
                level_liq = min(to_liquidate,bid.quantity)
                liq_value += level_liq * bid.price
                to_liquidate -= level_liq
                if to_liquidate == 0:
                    break
            return account.own_quote + liq_value

def score_inventory_value(self : Validator, uid : int, inventory_values : Dict[int, Dict[int,float]]) -> float:
    """
    Calculates the new score value for a specific UID

    Args:
        self (taos.im.neurons.validator.Validator) : Validator instance
        uid (int) : UID of miner being scored
        inventory_values (Dict[Dict[int, float]]) : Array of last `config.scoring.sharpe.lookback` inventory values for the miner

    Returns: 
        float: The new score value for the given UID.
    """
    if not self.sharpe_values[uid]: return 0.0
    sharpes = self.sharpe_values[uid]['books']
    normalized_sharpes = {book_id : normalize(self.config.scoring.sharpe.normalization_min, self.config.scoring.sharpe.normalization_max, sharpe) for book_id, sharpe in sharpes.items()}
    # The maximum volume to be traded by a miner in a `trade_volume_assessment_period` (24H) is `capital_turnover_cap` (10) times the initial miner capital
    volume_cap =  round(self.config.scoring.activity.capital_turnover_cap * (self.simulation.miner_wealth), self.simulation.volumeDecimals)
    # Calculate the volume traded by miners on each book in the period over which Sharpe values were calculated
    miner_volumes = {book_id : 0 for book_id in self.trade_volumes[uid]}
    for book_id, book_volume in self.trade_volumes[uid].items():
        for t, volume in sorted(book_volume['total'].items(), reverse=True):
            if t >= self.simulation_timestamp - self.config.scoring.sharpe.lookback * self.simulation.publish_interval:
                miner_volumes[book_id] += volume
            else:
                break
    # Calculate the factor to be multiplied on the Sharpes when there has been no trading activity in the previous Sharpe assessment window
    # This factor is designed to reduce the activity multiplier by half after each `sharpe.lookback` steps of inactivity
    inactivity_decay_factor = (2 ** (-1 / self.config.scoring.sharpe.lookback))
    latest_volumes = {book_id : list(book_volume['total'].values())[-1] if len(book_volume['total']) > 0 else 0.0 for book_id, book_volume in self.trade_volumes[uid].items()}
    # Calculate the activity factors to be multiplied onto the Sharpes to obtain the final values for assessment
    # If the miner has traded in the previous Sharpe assessment window, the factor is equal to the ratio of the miner trading volume to the cap
    # If the miner has not traded, their existing activity factor is decayed by the factor defined above so as to halve the miner score over each Sharpe assessment window where they remain inactive
    self.activity_factors[uid] = {book_id : min(1 + (miner_volume / volume_cap), 2.0) if latest_volumes[book_id] > 0 else self.activity_factors[uid][book_id] * inactivity_decay_factor for book_id, miner_volume in miner_volumes.items()}
    # Calculate the activity-weighted Sharpes by multiplying the activity factors onto the normalized volume-weighted Sharpes - this magnifies wins and losses occurring in periods with higher trading volumes
    activity_weighted_normalized_sharpes = [(activity_factor if activity_factor < 1 or normalized_sharpes[book_id] > 0.5 else 2 - activity_factor) * normalized_sharpes[book_id] for book_id, activity_factor in self.activity_factors[uid].items()]
    self.sharpe_values[uid]['books_weighted'] = {book_id : weighted_sharpe for book_id, weighted_sharpe in enumerate(activity_weighted_normalized_sharpes)}
    # Define a function which uses the 1.5 rule to detect left-hand outliers in the activity-weighted Sharpes
    def detect_outliers(sharpes):
        data = np.array(sharpes)
        q1 = np.percentile(data, 25)
        q3 = np.percentile(data, 75)
        iqr = q3 - q1
        lower_threshold = q1 - 1.5 * iqr
        outliers = data[data < lower_threshold]
        return outliers
    # Outliers detected here are activity-weighted Sharpes which are significantly lower than those achieved on other books
    outliers = detect_outliers(activity_weighted_normalized_sharpes)
    # A penalty equal to 67% of the difference between the mean outlier value and the value at the centre of the possible activity weighted Sharpe values is calculated
    outlier_penalty = (0.5 - np.mean(outliers))/1.5 if len(outliers) > 0 and np.mean(outliers) < 0.5 else 0
    # The median of the activity weighted Sharpes provides the base score for the miner
    activity_weighted_normalized_median = np.median(activity_weighted_normalized_sharpes)
    # The penalty factor is subtracted from the base score to punish particularly poor performance on any particular book
    sharpe_score = max(activity_weighted_normalized_median - abs(outlier_penalty), 0.0)
    
    self.sharpe_values[uid]['activity_weighted_normalized_median'] = activity_weighted_normalized_median
    self.sharpe_values[uid]['penalty'] = abs(outlier_penalty)
    self.sharpe_values[uid]['score'] = sharpe_score
    return self.reward_weights['sharpe'] * sharpe_score

def score_inventory_values(self, inventory_values):
    if self.config.scoring.sharpe.parallel_workers == 0:
        self.sharpe_values = {uid.item() : sharpe(uid, inventory_values[uid], self.config.scoring.sharpe.lookback, self.config.scoring.sharpe.normalization_min, self.config.scoring.sharpe.normalization_max, self.simulation.grace_period) for uid in self.metagraph.uids}
    else:
        num_processes = self.config.scoring.sharpe.parallel_workers
        batches = [self.metagraph.uids[i:i+int(256/num_processes)] for i in range(0,256,int(256/num_processes))]
        self.sharpe_values = batch_sharpe(inventory_values, batches, self.config.scoring.sharpe.lookback, self.config.scoring.sharpe.normalization_min, self.config.scoring.sharpe.normalization_max, self.simulation.grace_period, self.deregistered_uids)

    inventory_scores = {uid : score_inventory_value(self, uid, self.inventory_history[uid]) for uid in self.metagraph.uids}
    return inventory_scores

def reward(self : Validator, synapse : MarketSimulationStateUpdate) -> list[float]:
    """
    Calculate and store the scores for a particular miner.

    Args:
        self (taos.im.neurons.validator.Validator) : Validator instance
        synapse (taos.im.protocol.MarketSimulationStateUpdate) : The latest state update synapse

    Returns:
        list[float]: The new score values for all uids in the subnet.
    """
    for bookId, book in synapse.books.items():                
        trades = [event for event in book.events if isinstance(event, TradeInfo)]
        if trades:
            self.recent_trades[bookId].extend(trades)
            self.recent_trades[bookId] = self.recent_trades[bookId][-25:]
    for uid in self.metagraph.uids:
        try:
            sampled_timestamp = math.ceil(synapse.timestamp / self.config.scoring.activity.trade_volume_sampling_interval) * self.config.scoring.activity.trade_volume_sampling_interval
            # Prune the trading volume history of the miner to exclude values prior to the latest `trade_volume_assessment_period`
            for book_id, role_trades in self.trade_volumes[uid].items():
                for role, trades in role_trades.items():
                    for time in sorted(trades.keys()):
                        if time < synapse.timestamp - self.config.scoring.activity.trade_volume_assessment_period:                                
                            del self.trade_volumes[uid][book_id][role][time]
                        else:
                            break
                if not sampled_timestamp in self.trade_volumes[uid][book_id]['total']:
                    self.trade_volumes[uid][book_id]['total'][sampled_timestamp] = 0.0
                    self.trade_volumes[uid][book_id]['maker'][sampled_timestamp] = 0.0
                    self.trade_volumes[uid][book_id]['taker'][sampled_timestamp] = 0.0
                    self.trade_volumes[uid][book_id]['self'][sampled_timestamp] = 0.0
            # Update trade volume history with new trades since the previous step
            
            trades = [notice for notice in synapse.notices[uid] if notice.type in ['EVENT_TRADE',"ET"]]
            for trade in trades:
                roles = (["maker"] if trade.makerAgentId == uid else []) + (["taker"] if trade.takerAgentId == uid else [])
                for role in roles:
                    self.recent_miner_trades[trade.makerAgentId if role == "maker" else trade.takerAgentId][trade.bookId].append([trade, role])
                self.recent_miner_trades[uid][trade.bookId] = self.recent_miner_trades[uid][trade.bookId][-5:]
                
                self.trade_volumes[uid][trade.bookId]['total'][sampled_timestamp] = round(self.trade_volumes[uid][trade.bookId]['total'][sampled_timestamp] + trade.quantity * trade.price, self.simulation.volumeDecimals)
                if trade.makerAgentId == trade.takerAgentId:                
                    self.trade_volumes[uid][trade.bookId]['self'][sampled_timestamp] = round(self.trade_volumes[uid][trade.bookId]['self'][sampled_timestamp] + trade.quantity * trade.price, self.simulation.volumeDecimals)
                elif trade.makerAgentId == uid:
                    self.trade_volumes[uid][trade.bookId]['maker'][sampled_timestamp] = round(self.trade_volumes[uid][trade.bookId]['maker'][sampled_timestamp] + trade.quantity * trade.price, self.simulation.volumeDecimals)
                elif trade.takerAgentId == uid:
                    self.trade_volumes[uid][trade.bookId]['taker'][sampled_timestamp] = round(self.trade_volumes[uid][trade.bookId]['taker'][sampled_timestamp] + trade.quantity * trade.price, self.simulation.volumeDecimals)
            
            for bookId, account in synapse.accounts[uid].items():                    
                if self.initial_balances[uid][bookId]['BASE'] == None:
                    self.initial_balances[uid][bookId]['BASE'] = account.base_balance.total
                if self.initial_balances[uid][bookId]['QUOTE'] == None:
                    self.initial_balances[uid][bookId]['QUOTE'] = account.quote_balance.total
                if self.initial_balances[uid][bookId]['WEALTH'] == None:
                    self.initial_balances[uid][bookId]['WEALTH'] = get_inventory_value(synapse.accounts[uid][bookId], synapse.books[bookId])
            
            # Calculate the current value of the agent's inventory and append to the history array
            if uid in synapse.accounts:
                self.inventory_history[uid][synapse.timestamp] = {book_id : get_inventory_value(synapse.accounts[uid][book_id], book) - self.initial_balances[uid][book_id]['WEALTH'] for book_id, book in synapse.books.items()}
            else:
                self.inventory_history[uid][synapse.timestamp] = {book_id : 0.0 for book_id in synapse.books}
            # Prune the inventory history
            self.inventory_history[uid] = dict(list(self.inventory_history[uid].items())[-self.config.scoring.sharpe.lookback:])
        except Exception as ex:
            bt.logging.error(f"Failed to update reward data for UID {uid} at step {self.step} : {traceback.format_exc()}")
            
    inventory_scores = score_inventory_values(self, self.inventory_history)
    return list(inventory_scores.values())
    
def distribute_rewards(self : Validator, rewards : torch.FloatTensor):
    rng = np.random.default_rng(self.config.rewarding.seed)    
    distribution = torch.FloatTensor(sorted(self.config.rewarding.pareto.scale * rng.pareto(self.config.rewarding.pareto.shape, len(self.metagraph.uids))))
    sorted_rewards, sorted_indices = rewards.sort()
    distributed_rewards = distribution * sorted_rewards
    return torch.gather(distributed_rewards, 0, sorted_indices.argsort())
    
def get_rewards(
    self : Validator, synapse : MarketSimulationStateUpdate
) -> torch.FloatTensor:
    """
    Returns a tensor of rewards for the given query and responses.

    Args:
        self (taos.im.neurons.validator.Validator) : Validator instance
        synapse (taos.im.protocol.MarketSimulationStateUpdate) : The latest state update object.

    Returns:
        torch.FloatTensor: A tensor of rewards for the given query and responses.
    """
    # Get all the reward results by iteratively calling the reward() function.
    return distribute_rewards(self, torch.FloatTensor(reward(self, synapse)).to(self.device))

def set_delays(self: Validator, synapse_responses: dict[int, MarketSimulationStateUpdate]) -> list[FinanceAgentResponse]:
    """
    Applies base delay based on process time using an exponential mapping,
    and adds a per-book Gaussian-distributed random latency instruction_delay to instructions,
    with zero instruction_delay applied to the first instruction per book.

    Args:
        self (taos.im.neurons.validator.Validator): Validator instance.
        synapse_responses (dict[int, MarketSimulationStateUpdate]): Latest state updates.

    Returns:
        list[FinanceAgentResponse]: Delayed finance responses.
    """
    responses = []
    timeout = self.config.neuron.timeout
    min_delay = self.config.scoring.min_delay
    max_delay = self.config.scoring.max_delay
    min_instruction_delay = self.config.scoring.min_instruction_delay
    max_instruction_delay = self.config.scoring.max_instruction_delay

    def compute_delay(p_time: float) -> int:
        """Exponential scaling of process time into delay."""
        t = p_time / timeout
        exp_scale = 5
        delay_frac = (np.exp(exp_scale * t) - 1) / (np.exp(exp_scale) - 1)
        delay = min_delay + delay_frac * (max_delay - min_delay)
        return int(delay)

    for uid, synapse_response in synapse_responses.items():
        response = synapse_response.response
        if response:
            base_delay = compute_delay(synapse_response.dendrite.process_time)

            seen_books = set()
            for instruction in response.instructions:
                book_id = instruction.bookId

                # Zero instruction_delay for first instruction per book
                if book_id not in seen_books:
                    instruction_delay = 0
                    seen_books.add(book_id)
                else:
                    instruction_delay = random.randint(min_instruction_delay, max_instruction_delay)

                instruction.delay += base_delay + instruction_delay

            responses.append(response)
            bt.logging.info(
                f"UID {response.agent_id} responded with {len(response.instructions)} instructions "
                f"after {synapse_response.dendrite.process_time:.4f}s – base delay {base_delay}{self.simulation.time_unit}"
            )

    return responses