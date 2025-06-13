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
import bittensor as bt
import numpy as np
from typing import List, Dict
from taos.im.neurons.validator import Validator
from taos.im.protocol import MarketSimulationStateUpdate, FinanceAgentResponse
from taos.im.protocol.models import Account, Book

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
            return account.quote_balance.total + (book.bids[0].price if len(book.asks) > 0 and len(book.bids) > 0 else 0.0) * account.base_balance.total
        case "midquote":
            return account.quote_balance.total + ((book.asks[0].price + book.bids[0].price) / 2 if len(book.asks) > 0 and len(book.bids) > 0 else 0.0) * account.base_balance.total
        case "liquidation":
            liq_value = 0
            to_liquidate = account.base_balance.total
            for bid in book.bids:
                level_liq = min(to_liquidate,bid.quantity)
                liq_value += level_liq * bid.price
                to_liquidate -= level_liq
                if to_liquidate == 0:
                    break
            return account.quote_balance.total + liq_value
        
def normalize(lower, upper, value):
    return (max(min(value, upper), lower) + upper) / (upper - lower)
        
def sharpe(self : Validator, uid : int, inventory_values : Dict[int, Dict[int,float]]) -> dict:
    """
    Calculates intraday Sharpe ratios for a particular UID using the change in inventory values over previous `config.scoring.sharpe.lookback` observations to represent returns.
    Values are also stored to a property of the Validator class to be accessed later for scoring and reporting purposes.

    Args:
        self (taos.im.neurons.validator.Validator) : Validator instance
        uid (int) : UID of miner being scored
        inventory_values (Dict[Dict[int, float]]) : Array of last `config.scoring.sharpe.lookback` inventory values for the miner

    Returns:
    dict: A dictionary containing all relevant calculated Sharpe values for the UID.  This includes Sharpe for their total inventory value and Sharpe calculated on each book along with
          several aggregate values obtained from the values for each book and their normalized counterparts.
    """
    try:
        if len(inventory_values) <= 1: return 0.0
        # Calculate the per-book Sharpe ratio values
        book_inventory_values = {bookId : np.array([inventory_value[bookId] for inventory_value in inventory_values.values() if bookId in inventory_value]) for bookId in list(inventory_values.values())[-1]}
        timestamps = list(inventory_values.keys())
        changeover = [i for i in range(len(timestamps)-1) if timestamps[i+1] < timestamps[i]]
        for bookId, book_inventory_value in book_inventory_values.items():
            if len(book_inventory_value) > 1:
                returns = (book_inventory_value[1:] - book_inventory_value[:-1])
                if len(changeover) > 0:
                    returns = np.delete(returns, changeover)
                returns = returns[-self.config.scoring.sharpe.lookback:]
                mean = sum(returns) / len(returns) if len(returns) > 0 else 0.0
                variance = sum([((x - mean) ** 2) for x in returns]) / len(returns) if len(returns) > 0 else 0.0
                std = variance ** 0.5
                sharpe = math.sqrt(len(returns)) * (mean / std) if std != 0.0 else 0.0
                self.sharpe_values[uid]['books'][bookId] = sharpe
            else:
                self.sharpe_values[uid]['books'][bookId] = sharpe
        # Calculate the total Sharpe ratio value using inventories summed over all books
        total_inventory_values = np.array([sum(list(inventory_value.values())) for inventory_value in inventory_values.values()])
        if len(total_inventory_values) > 1:
            returns = (total_inventory_values[1:] - total_inventory_values[:-1])
            if len(changeover) > 0:
                returns = np.delete(returns, changeover)
            returns = returns[-self.config.scoring.sharpe.lookback:]
            mean = sum(returns) / len(returns) if len(returns) > 0 else 0.0
            variance = sum([((x - mean) ** 2) for x in returns]) / len(returns) if len(returns) > 0 else 0.0
            std = variance ** 0.5
            total_sharpe = math.sqrt(len(returns)) * (mean / std) if std != 0.0 else 0.0   
        else:
            total_sharpe = 0.0
        self.sharpe_values[uid]['total'] = total_sharpe
        # Calculate the average Sharpe ratio value over all books
        all_sharpes = [sharpe for bookId, sharpe in self.sharpe_values[uid]['books'].items()]
        avg_sharpe = sum(all_sharpes) / len(all_sharpes)
        self.sharpe_values[uid]['average'] = avg_sharpe
        median_sharpe = np.median(all_sharpes)
        self.sharpe_values[uid]['median'] = median_sharpe
        
        # In order to produce non-zero values for Sharpe ratio of the agent, the result of the usual calculation is normalized to fall within a configured range.
        # This allows to simply multiply scaling factors onto the resulting score, enabling straighforward zeroing of weights for agents behaving undesirably by other assessments.
        # This also enforces a cap which eliminates extreme Sharpe values which would be considered unrealistic.
        normalized_avg_sharpe = normalize(self.config.scoring.sharpe.normalization_min, self.config.scoring.sharpe.normalization_max, avg_sharpe)
        normalized_total_sharpe = normalize(self.config.scoring.sharpe.normalization_min, self.config.scoring.sharpe.normalization_max, total_sharpe)
        normalized_median_sharpe = normalize(self.config.scoring.sharpe.normalization_min, self.config.scoring.sharpe.normalization_max, median_sharpe)
        self.sharpe_values[uid]['normalized_average'] = normalized_avg_sharpe
        self.sharpe_values[uid]['normalized_total'] = normalized_total_sharpe
        self.sharpe_values[uid]['normalized_median'] = normalized_median_sharpe
        return self.sharpe_values[uid]
    except Exception as ex:
        bt.logging.error(f"Failed to calculate Sharpe for UID {uid} : {traceback.format_exc()}")

def score_inventory_values(self : Validator, uid : int, inventory_values : Dict[int, Dict[int,float]]) -> float:
    """
    Calculates the new score value for a specific UID

    Args:
        self (taos.im.neurons.validator.Validator) : Validator instance
        uid (int) : UID of miner being scored
        inventory_values (Dict[Dict[int, float]]) : Array of last `config.scoring.sharpe.lookback` inventory values for the miner

    Returns: 
        float: The new score value for the given UID.
    """
    sharpe_data = sharpe(self, uid, inventory_values)
    sharpes = sharpe_data['books']
    normalized_sharpes = {book_id : normalize(self.config.scoring.sharpe.normalization_min, self.config.scoring.sharpe.normalization_max, sharpe) for book_id, sharpe in sharpes.items()}
    # The maximum volume to be traded by a miner in a `trade_volume_assessment_period` (24H) is `capital_turnover_cap` (10) times the initial miner capital
    volume_cap =  round(self.config.scoring.activity.capital_turnover_cap * (self.simulation.miner_wealth), self.simulation.volumeDecimals)
    # Calculate the volume traded by miners on each book in the period over which Sharpe values were calculated
    miner_volumes = {book_id : round(sum([volume for time, volume in book_volume['total'].items() if time >= self.simulation_timestamp - self.config.scoring.sharpe.lookback * self.simulation.publish_interval]), self.simulation.volumeDecimals) for book_id, book_volume in self.trade_volumes[uid].items()}
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
    # A penalty equal to the difference between the lowest outlier value and the value at the centre of the possible activity weighted Sharpe values is calculated
    outlier_penalty = np.min(outliers) if len(outliers) > 0 and np.min(outliers) < 0.0 else 0
    # The median of the activity weighted Sharpes provides the base score for the miner
    activity_weighted_normalized_median = np.median(activity_weighted_normalized_sharpes)
    # The penalty factor is subtracted from the base score to punish particularly poor performance on any particular book
    sharpe_score = activity_weighted_normalized_median - abs(outlier_penalty)
    
    return self.reward_weights['sharpe'] * sharpe_score

def reward(self : Validator, synapse : MarketSimulationStateUpdate, uid : int) -> float:
    """
    Calculate and store the scores for a particular miner.

    Args:
        self (taos.im.neurons.validator.Validator) : Validator instance
        synapse (taos.im.protocol.MarketSimulationStateUpdate) : The latest state update synapse
        uid (int) : UID of miner being scored

    Returns:
        float: The new score value for the miner.
    """
    try:
        sampled_timestamp = math.ceil(synapse.timestamp / self.config.scoring.activity.trade_volume_sampling_interval) * self.config.scoring.activity.trade_volume_sampling_interval
        # Prune the trading volume history of the miner to exclude values prior to the latest `trade_volume_assessment_period`
        for book_id, role_trades in self.trade_volumes[uid].items():
            for role, trades in role_trades.items():
                if trades != {}:
                    if min(trades.keys()) < synapse.timestamp - self.config.scoring.activity.trade_volume_assessment_period:
                        self.trade_volumes[uid][book_id][role] = {time : volume for time, volume in trades.items() if time > synapse.timestamp - self.config.scoring.activity.trade_volume_assessment_period}
                if not sampled_timestamp in self.trade_volumes[uid][book_id]['total']:
                    self.trade_volumes[uid][book_id]['total'][sampled_timestamp] = 0.0
                    self.trade_volumes[uid][book_id]['maker'][sampled_timestamp] = 0.0
                    self.trade_volumes[uid][book_id]['taker'][sampled_timestamp] = 0.0
                    self.trade_volumes[uid][book_id]['self'][sampled_timestamp] = 0.0
        # Update trade volume history with new trades since the previous step
        
        for notice in synapse.notices[uid]:
            if notice.type == 'EVENT_TRADE':
                self.trade_volumes[uid][notice.bookId]['total'][sampled_timestamp] = round(self.trade_volumes[uid][notice.bookId]['total'][sampled_timestamp] + notice.quantity * notice.price, self.simulation.volumeDecimals)
                if notice.makerAgentId == notice.takerAgentId:                
                    self.trade_volumes[uid][notice.bookId]['self'][sampled_timestamp] = round(self.trade_volumes[uid][notice.bookId]['self'][sampled_timestamp] + notice.quantity * notice.price, self.simulation.volumeDecimals)
                elif notice.makerAgentId == uid:
                    self.trade_volumes[uid][notice.bookId]['maker'][sampled_timestamp] = round(self.trade_volumes[uid][notice.bookId]['maker'][sampled_timestamp] + notice.quantity * notice.price, self.simulation.volumeDecimals)
                elif notice.takerAgentId == uid:
                    self.trade_volumes[uid][notice.bookId]['taker'][sampled_timestamp] = round(self.trade_volumes[uid][notice.bookId]['taker'][sampled_timestamp] + notice.quantity * notice.price, self.simulation.volumeDecimals)
        
        # Calculate the current value of the agent's inventory and append to the history array
        if uid in synapse.accounts:
            self.inventory_history[uid][synapse.timestamp] = {book_id : get_inventory_value(synapse.accounts[uid][book_id], book) - self.simulation.miner_wealth for book_id, book in synapse.books.items()}
        else:
            self.inventory_history[uid][synapse.timestamp] = {book_id : 0.0 for book_id in synapse.books}
        # Prune the inventory history
        self.inventory_history[uid] = dict(list(self.inventory_history[uid].items())[-self.config.scoring.sharpe.lookback:])        
        
        inventory_score = score_inventory_values(self, uid, self.inventory_history[uid])        
        return inventory_score
    except Exception as ex:
        bt.logging.error(f"Failed to calculate reward for UID {uid} at step {self.step} : {traceback.format_exc()}")
        return self.scores[uid]

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
    # Get all the reward results by iteratively calling your reward() function.
    return torch.FloatTensor(
        [reward(self, synapse, uid.item()) for uid in self.metagraph.uids]
    ).to(self.device)

def set_delays(self : Validator, synapse_responses : dict[int, MarketSimulationStateUpdate]) -> list[FinanceAgentResponse]:
    """
    Calculates and applies the simulation time delay to be applied to each instruction received by the validator

    Args:
        self (taos.im.neurons.validator.Validator) : Validator instance
        synapse_responses (list[taos.im.protocol.MarketSimulationStateUpdate]) : The latest state update object.

    Returns:
        torch.FloatTensor: A tensor of rewards for the given query and responses.
    """
    responses = []
    for uid, synapse_response in synapse_responses.items():
        response = synapse_response.response
        if response:
            # Delay is calculated to be proportional to the configured maximum in the same proportion as the response time to the timeout
            delay = max(int((self.config.scoring.max_delay * (synapse_response.dendrite.process_time / self.config.neuron.timeout))),self.config.scoring.min_delay)
            for instruction in response.instructions:
                instruction.delay = delay + instruction.delay
            responses.append(response)
            bt.logging.debug(f"UID {response.agent_id} Responded with {len(response.instructions)} instructions after {synapse_response.dendrite.process_time:.4f}s - delay set to {delay}{self.simulation.time_unit}")
    return responses