# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
from loky.backend.context import set_start_method
set_start_method('forkserver', force=True)
from loky import get_reusable_executor

from taos.im.utils import normalize

def sharpe(uid, inventory_values, lookback, norm_min, norm_max, grace_period, deregistered_uids) -> dict:
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
    import traceback
    import numpy as np
    try:
        if uid in deregistered_uids or len(inventory_values) < min(720, lookback): return None
        sharpe_values = {'books' : {}}
        # Calculate the per-book Sharpe ratio values
        np_inventory_values = np.array([list(iv.values()) for iv in inventory_values.values()]).T
        timestamps = list(inventory_values.keys())
        changeover = [i for i in range(len(timestamps)-1) if timestamps[i+1] >= timestamps[i] + grace_period]
        bookId = 0
        for book_inventory_values in np_inventory_values:
            returns = (book_inventory_values[1:] - book_inventory_values[:-1])
            if len(changeover) > 0:
                returns = np.delete(returns, changeover)
            std = returns.std()
            sharpe_values['books'][bookId] = np.sqrt(len(returns)) * (returns.mean() / std) if std != 0.0 else 0.0
            bookId += 1
        
        # Calculate the average Sharpe ratio value over all books
        all_sharpes = np.array(list(sharpe_values['books'].values()))
        sharpe_values['average'] = all_sharpes.mean()
        sharpe_values['median'] = np.median(all_sharpes)            
            
        # Calculate the total Sharpe ratio value using inventories summed over all books
        total_inventory_values = np.array([sum(list(inventory_value.values())) for inventory_value in inventory_values.values()])
        returns = (total_inventory_values[1:] - total_inventory_values[:-1])
        if len(changeover) > 0:
            returns = np.delete(returns, changeover)
        std = returns.std()
        sharpe_values['total'] = np.sqrt(len(returns)) * (returns.mean() / std) if std != 0.0 else 0.0
        
        # In order to produce non-zero values for Sharpe ratio of the agent, the result of the usual calculation is normalized to fall within a configured range.
        # This allows to simply multiply scaling factors onto the resulting score, enabling straighforward zeroing of weights for agents behaving undesirably by other assessments.
        # This also enforces a cap which eliminates extreme Sharpe values which would be considered unrealistic.
        sharpe_values['normalized_average'] = normalize(norm_min, norm_max, sharpe_values['average'])
        sharpe_values['normalized_median'] = normalize(norm_min, norm_max, sharpe_values['median'])
        sharpe_values['normalized_total'] = normalize(norm_min, norm_max, sharpe_values['total'])
        return sharpe_values
    except Exception as ex:
        print(f"Failed to calculate Sharpe for UID {uid} : {traceback.format_exc()}")
        return None
    
def sharpe_batch(inventory_values, lookback, norm_min, norm_max, grace_period, deregistered_uids):
    return {uid : sharpe(uid, inventory_value, lookback, norm_min, norm_max, grace_period, deregistered_uids) for uid, inventory_value in inventory_values.items()}

def batch_sharpe(inventory_values, batches, lookback, norm_min, norm_max, grace_period, deregistered_uids):
    sharpe_batches= []
    pool = get_reusable_executor(max_workers=len(batches))
    tasks = [pool.submit(sharpe_batch, {uid : inventory_values[uid] for uid in batch}, lookback, norm_min, norm_max, grace_period, deregistered_uids) for batch in batches]
    for task in tasks:
        result = task.result()
        sharpe_batches.append(result)
    return {int(k): v for d in sharpe_batches for k, v in d.items()}