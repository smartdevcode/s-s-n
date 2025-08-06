# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT

import time
import pandas as pd
import numpy as np
import bittensor as bt
from threading import Thread
from copy import deepcopy
from pathlib import Path

from taos.common.agents import launch
from taos.im.agents.ai.regressor import FinanceSimulationAIRegressorAgent
from taos.im.protocol.models import *
from taos.im.protocol.instructions import *
from taos.im.protocol import MarketSimulationStateUpdate, FinanceAgentResponse

from sklearn.metrics import accuracy_score

"""
An example of a basic AI trading agent that collects market data and predicts 
future returns using a simple regression model.
"""

class SimpleRegressorAgent(FinanceSimulationAIRegressorAgent):
    def print_config(self):
        """Prints the agent's current strategy configuration."""
        bt.logging.info(f"""
---------------------------------------------------------------
Strategy Config
---------------------------------------------------------------
Order Quantity              : {self.quantity}
Order Expiry               : {self.expiry_period}ns
Model                      : {self.model}
Model Parameters:
""" + '\n'.join([f"\t{name} : {val}" for name, val in self.model_kwargs.items()]) + f"""
Checkpoint                 : {self.checkpoint if self.checkpoint else 'N/A'}
Pretraining Enabled        : {self.should_pretrain}
Sampling Interval          : {self.sampling_interval}s
Training Observations      : {self.train_n}
Training Interval          : {self.train_interval}
Minimum Training Runs      : {self.min_train_events}
Predictor Features         : {self.predKeys}
Target Features            : {self.targetKeys}
Output Directory           : {self.output_dir}
---------------------------------------------------------------""")

    def initialize(self):
        """
        Initializes the agent by setting configuration values,
        model parameters, and data containers.
        """
        # Quantity of BASE to attempt to buy/sell at each round
        self.quantity = self.config.quantity if hasattr(self.config,'quantity') else 1.0
        # Expiry period for limit orders in simulation nanoseconds
        self.expiry_period = self.config.expiry_period if hasattr(self.config,'expiry_period') else 120e9

        # Define the list of variables which will be used for prediction.  These are populated in the `update_predictors` method.
        # This listing contains several of the most commonly applied features.
        # Note that if modifying the features, you must define the calculation of any new features in `update_predictors`.
        self.predKeys = ['Open', 'High', 'Low', 'Close', 'Volume', 'Direction', 'TradeImbalance', 'OrderImbalance', 'AvgReturn']
        # Define the list of variables which will be predicted by the model.  These are also defined and populated in the `update_predictors` method
        # A common and natural target is to predict the return for the next step.
        self.targetKeys = ['LogReturn']

        # Prepare common variables and execute pretraining if specified
        # Allowed options fpr the `model` parameter for this example strategy are
        # `ElasticNet`, `Lasso`, `PassiveAggressiveRegressor`, `MLP`
        # In the first two cases the selected model is used to apply penalty during Stochastic Gradient Descent for an SGD regressor model
        self.model = self.config.model if hasattr(self.config,'model') else "MLP"
        self.prepare(self.model)

        # Initialize storage for prediction and analysis
        self.predictors = {}
        self.target = {}
        self.last_signal = {}
        self.midquotes = {}
        self.signs = {}
        self.trueSigns = {}
        self.errors = {}
        self.event_history = None

    def update_predictors(self, book: Book, timestamp: int) -> None:
        """
        Gathers and processes historical event data to update predictors and targets.
        Features are computed based on sampling intervals.

        Args:
            book (Book): Book object from the state update.
            timestamp (dict): Simulation timestamp of the associated state update.
        """
        if not self.event_history:
            lookback_minutes = max((self.simulation_config.publish_interval // 1_000_000_000) // 60, self.sampling_interval * 2 // 60, 1)
            self.event_history = book.event_history(timestamp, self.simulation_config, lookback_minutes)
        else:
            book.append_to_event_history(timestamp, self.event_history, self.simulation_config)

        # Aggregate data for features
        ohlc = self.event_history.ohlc(self.sampling_interval)
        sampled_mean_price = self.event_history.mean_trade_price(self.sampling_interval)

        trade_buckets = self.event_history.bucket(self.event_history.trades, self.sampling_interval)
        sampled_trade_volumes = {ts: sum(t.quantity for t in trades) for ts, trades in trade_buckets.items()}
        sampled_trade_imb = {ts: sum(t.quantity for t in trades if t.side == OrderDirection.BUY) - sum(t.quantity for t in trades if t.side == OrderDirection.SELL) for ts, trades in trade_buckets.items()}

        order_buckets = self.event_history.bucket(self.event_history.orders, self.sampling_interval)
        sampled_order_volumes = {ts: sum(o.quantity for o in orders) for ts, orders in order_buckets.items()}
        sampled_order_imbalances = {ts: sum(o.quantity for o in orders if o.side == OrderDirection.BUY) - sum(o.quantity for o in orders if o.side == OrderDirection.SELL) for ts, orders in order_buckets.items()}

        # Determine the number of new data points in the history
        n_new = max((self.simulation_config.publish_interval // 1_000_000_000) // self.sampling_interval, 1)
        new_sampled_data = {
            ts: {
                "ohlc": ohlc[ts],
                "mean_price": sampled_mean_price[ts],
                "trade_volume": sampled_trade_volumes[ts],
                "trade_imb": sampled_trade_imb[ts],
                "order_volume": sampled_order_volumes[ts],
                "order_imbalance": sampled_order_imbalances[ts]
            } for ts in list(ohlc.keys())[-n_new:]
        }

        # Calculate predictor and target values
        new_predictors = {key: [] for key in self.predKeys}
        new_targets = {key: [] for key in self.targetKeys}

        for ts, sampled in new_sampled_data.items():
            if not sampled['ohlc']:
                continue
            new_predictors['Open'].append(sampled['ohlc']['open'])
            new_predictors['Low'].append(sampled['ohlc']['low'])
            new_predictors['High'].append(sampled['ohlc']['high'])
            new_predictors['Close'].append(sampled['ohlc']['close'])
            new_predictors['Direction'].append(
                -1 if sampled['ohlc']['close'] < sampled['ohlc']['open']
                else (1 if sampled['ohlc']['close'] > sampled['ohlc']['open'] else 0)
            )
            new_predictors['Volume'].append(sampled['order_volume'])
            new_predictors['TradeImbalance'].append(
                sampled['trade_imb'] / sampled['trade_volume'] if sampled['trade_volume'] else 0.0
            )
            new_predictors['OrderImbalance'].append(
                sampled['order_imbalance'] / sampled['order_volume'] if sampled['order_volume'] else 0.0
            )
            new_predictors['AvgReturn'].append(
                1 - sampled['mean_price'] / sampled['ohlc']['high']
            )

            new_targets['LogReturn'].append(
                np.log(sampled['ohlc']['close'] / sampled['ohlc']['open'])
            )

        # Update stored data
        self.predictors[book.id] = {
            key: self.predictors[book.id].get(key, []) + new_predictors[key]
            for key in self.predKeys
        }
        self.target[book.id] = {
            key: self.target[book.id].get(key, []) + new_targets[key]
            for key in self.targetKeys
        }

        # Prune to recent history
        self.predictors[book.id] = {
            key: values[-self.train_n - 3:] for key, values in self.predictors[book.id].items()
        }
        self.target[book.id] = {
            key: values[-self.train_n - 3:] for key, values in self.target[book.id].items()
        }

        # Record for logging or training
        self.record_data(book.id, {'predictors': new_predictors, 'target': new_targets})

    def signal(self, predictions: dict[str, float]) -> float:
        """
        Converts model predictions into a trading signal.
        Caps predictions to avoid overconfidence when the model is undertrained.

        Args:
            predictions (dict[str, float]): Dictionary mapping target name to the latest predicted value.
        """
        signal = predictions['LogReturn']
        if abs(signal) > 0.5:
            signal = 0.5 * np.sign(signal)
            bt.logging.info("Warning: Prediction magnitude exceeded threshold — more training required.")
        return signal

    def respond(self, state: MarketSimulationStateUpdate) -> FinanceAgentResponse:
        """
        Called upon new market state updates. The core strategy loop:
        - Extracts market data
        - Updates predictors
        - Predicts returns
        - Places limit orders based on predicted signal

        Args:
            state (taos.im.protocol.MarketSimulationStateUpdate): The class representing the latest state of the simulation.

        Returns:
            taos.im.protocol.FinanceAgentResponse : The response which will be attached to the synapse for return to the querying validator.
        """
        response = FinanceAgentResponse(agent_id=self.uid)
        start = time.time()

        for book_id, book in state.books.items():
            bestBid = book.bids[0].price if book.bids else 0.0
            bestAsk = book.asks[0].price if book.asks else bestBid + 10 ** (-self.simulation_config.priceDecimals)
            midquote = (bestBid + bestAsk) / 2

            # Initialization for unseen books
            if book_id not in self.predictors:
                self.predictors[book_id] = {key: [] for key in self.predKeys}
                self.target[book_id] = {key: [] for key in self.targetKeys}
                self.last_signal[book_id] = 0.0
                self.midquotes[book_id] = 0.0
                self.signs[book_id] = []
                self.trueSigns[book_id] = []
                self.errors[book_id] = []
                self.init_book(book_id)

            self.update_predictors(book, state.timestamp)

            if not self.model_trained[book_id] or len(self.predictors[book_id][self.predKeys[0]]) < 1:
                bt.logging.info(f"BOOK {book_id}: Training Progress {self.trained_events[book_id]}/{self.min_train_events}")
                continue

            # Prepare latest predictor sample
            predictors = {key: self.predictors[book_id][key][-1] for key in self.predKeys}
            X = pd.DataFrame(predictors, index=[0])

            # Make predictions
            predictions = dict(zip(self.targetKeys, self.models[book_id].predict(X)))
            signal = self.signal(predictions)

            bt.logging.info(f"BOOK {book_id}: PREDICTION " +
                            ", ".join([f"{k}: {v:.4f}" for k, v in predictions.items()]) +
                            f" | SIGNAL {signal:.4f}")

            # Track performance
            if self.midquotes[book_id] != 0.0:
                curr_return = np.log(midquote / self.midquotes[book_id])
                self.errors[book_id].append((self.last_signal[book_id] - curr_return) ** 2)
                self.signs[book_id].append(np.sign(self.last_signal[book_id]))
                self.trueSigns[book_id].append(np.sign(curr_return))
                bt.logging.info(
                    f"BOOK {book_id}: RETURN {curr_return:.4f} | "
                    f"MSE: {np.mean(self.errors[book_id]):.4f} | "
                    f"ACC: {accuracy_score(self.trueSigns[book_id], self.signs[book_id]):.2f}"
                )

            self.last_signal[book_id] = signal
            self.midquotes[book_id] = midquote

            # Trading logic
            dec = self.simulation_config.priceDecimals
            if signal > 0:
                # If the signal is positive, firstly place a buy order just above the current best bid level
                response.limit_order(
                    book_id,
                    OrderDirection.BUY,
                    self.quantity,
                    round(bestBid + 10**(-self.simulation_config.priceDecimals), self.simulation_config.priceDecimals),
                    timeInForce=TimeInForce.GTT, expiryPeriod=self.expiry_period
                )
                # Place a sell order with distance from midquote proportional to the strength of the prediction
                response.limit_order(
                    book_id,
                    OrderDirection.SELL,
                    self.quantity,
                    round(midquote*np.exp(signal),self.simulation_config.priceDecimals),
                    timeInForce=TimeInForce.GTT, expiryPeriod=self.expiry_period
                )
            elif signal < 0:
                # If the signal is negative, firstly place a sell order just below the current best ask level
                response.limit_order(
                    book_id,
                    OrderDirection.SELL,
                    self.quantity,
                    round(bestAsk - 10**(self.simulation_config.priceDecimals),self.simulation_config.priceDecimals),
                    timeInForce=TimeInForce.GTT, expiryPeriod=self.expiry_period
                )
                # Place a buy order with distance from midquote proportional to the strength of the prediction
                response.limit_order(
                    book_id,
                    OrderDirection.BUY,
                    self.quantity,
                    round(midquote*np.exp(signal),self.simulation_config.priceDecimals),
                    timeInForce=TimeInForce.GTT, expiryPeriod=self.expiry_period
                )

            # Launch asynchronous model update
            Thread(target=self.update_model, args=(book_id,)).start()

        bt.logging.info(f"Response Generated in {time.time() - start:.2f}s")
        return response


if __name__ == "__main__":
    """
    Example command for local standalone execution:
    python SimpleRegressorAgent.py --port 8888 --agent_id 0 --params quantity=10.0 expiry_period=120000000000 model=PassiveAggressiveRegressor sampling_interval=1 train_periods=60 train_n=100
    """
    launch(SimpleRegressorAgent)
