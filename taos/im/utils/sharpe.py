# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import numpy as np
import traceback
from loky.backend.context import set_start_method
set_start_method('forkserver', force=True)
from loky import get_reusable_executor

from taos.im.utils import normalize


def sharpe(uid, inventory_values, lookback, norm_min, norm_max, min_lookback, grace_period, deregistered_uids) -> dict:
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
        num_values = len(inventory_values)
        if uid in deregistered_uids or num_values < min(min_lookback, lookback):
            return None
        
        # Pre-allocate result dictionary
        sharpe_values = {'books': {}}
        
        # Extract inventory values and timestamps once
        inv_items = list(inventory_values.items())
        timestamps = [t for t, _ in inv_items]
        book_ids = sorted(next(iter(inventory_values.values())).keys())
        values_list = [[v[book_id] for book_id in book_ids] for _, v in inv_items]
        
        # Convert to numpy array (transposed for per-book access)
        np_inventory_values = np.array(values_list).T
        
        # Calculate changeover indices once
        changeover_mask = None
        if grace_period > 0:
            # Find indices where there's a grace period gap
            changeover = []
            for i in range(len(timestamps) - 1):
                if timestamps[i + 1] >= timestamps[i] + grace_period:
                    changeover.append(i)
            
            if changeover:
                # Create a boolean mask for efficient deletion
                changeover_mask = np.ones(len(timestamps) - 1, dtype=bool)
                changeover_mask[changeover] = False
        
        # Calculate per-book Sharpe ratios
        book_sharpes = []
        for bookId, book_inventory_values in enumerate(np_inventory_values):
            # Calculate returns
            returns = np.diff(book_inventory_values)
            
            # Apply changeover mask if needed
            if changeover_mask is not None:
                returns = returns[changeover_mask]
            
            # Calculate Sharpe ratio
            std = returns.std()
            sharpe_val = np.sqrt(len(returns)) * (returns.mean() / std) if std != 0.0 else 0.0
            
            sharpe_values['books'][bookId] = sharpe_val
            book_sharpes.append(sharpe_val)
        
        # Convert to numpy array for vectorized operations
        all_sharpes = np.array(book_sharpes)
        sharpe_values['average'] = all_sharpes.mean()
        sharpe_values['median'] = np.median(all_sharpes)
        
        # Calculate total Sharpe ratio (sum across books)
        # Use pre-computed values_list instead of reprocessing inventory_values
        total_inventory_values = np.sum(np_inventory_values, axis=0)
        returns = np.diff(total_inventory_values)
        
        # Apply changeover mask if needed
        if changeover_mask is not None:
            returns = returns[changeover_mask]
        
        std = returns.std()
        sharpe_values['total'] = np.sqrt(len(returns)) * (returns.mean() / std) if std != 0.0 else 0.0
        
        # Normalize values
        sharpe_values['normalized_average'] = normalize(norm_min, norm_max, sharpe_values['average'])
        sharpe_values['normalized_median'] = normalize(norm_min, norm_max, sharpe_values['median'])
        sharpe_values['normalized_total'] = normalize(norm_min, norm_max, sharpe_values['total'])
        
        return sharpe_values
    except Exception as ex:
        print(f"Failed to calculate Sharpe for UID {uid} : {traceback.format_exc()}")
        return None


def sharpe_batch(inventory_values, lookback, norm_min, norm_max, min_lookback, grace_period, deregistered_uids):
    """Process a batch of UIDs for Sharpe calculation"""
    return {uid: sharpe(uid, inventory_value, lookback, norm_min, norm_max, min_lookback, grace_period, deregistered_uids) 
            for uid, inventory_value in inventory_values.items()}


def batch_sharpe(inventory_values, batches, lookback, norm_min, norm_max, min_lookback, grace_period, deregistered_uids):
    """Parallel processing of Sharpe calculations across multiple batches"""
    pool = get_reusable_executor(max_workers=len(batches))
    
    # Submit all tasks
    tasks = [pool.submit(sharpe_batch, 
                        {uid: inventory_values[uid] for uid in batch}, 
                        lookback, norm_min, norm_max, min_lookback, grace_period, deregistered_uids) 
             for batch in batches]
    
    # Collect results and merge into single dictionary
    result = {}
    for task in tasks:
        batch_result = task.result()
        for k, v in batch_result.items():
            result[int(k)] = v
    
    return result