# SPDX-FileCopyrightText: 2025 Advanced Trading Agent for τaos Subnet 79
# SPDX-License-Identifier: MIT

import time
import numpy as np
import pandas as pd
import bittensor as bt
from typing import Dict, List, Optional, Tuple
from collections import defaultdict, deque
from dataclasses import dataclass
from threading import Thread
import json
import logging

from taos.common.agents import launch
from taos.im.agents import FinanceSimulationAgent, StateHistoryManager
from taos.im.protocol.models import *
from taos.im.protocol.instructions import *
from taos.im.protocol import MarketSimulationStateUpdate, FinanceAgentResponse
from taos.im.utils import duration_from_timestamp

# Machine Learning imports
from sklearn.ensemble import RandomForestRegressor, GradientBoostingRegressor
from sklearn.linear_model import Ridge, Lasso, ElasticNet
from sklearn.preprocessing import StandardScaler, RobustScaler
from sklearn.metrics import mean_squared_error, r2_score
import joblib
import warnings
warnings.filterwarnings('ignore')

@dataclass
class TradingSignal:
    """Represents a trading signal with confidence and risk metrics."""
    direction: int  # 1 for buy, -1 for sell, 0 for hold
    strength: float  # Signal strength (0-1)
    confidence: float  # Model confidence (0-1)
    risk_score: float  # Risk assessment (0-1)
    expected_return: float  # Expected return
    stop_loss: Optional[float] = None
    take_profit: Optional[float] = None

class AdvancedTradingAgent(FinanceSimulationAgent):
    """
    Advanced trading agent with multiple strategies, risk management, and ML capabilities.
    
    Features:
    - Multiple trading strategies (momentum, mean reversion, arbitrage)
    - Machine learning-based signal generation
    - Comprehensive risk management
    - Dynamic position sizing
    - Real-time performance monitoring
    - Adaptive strategy selection
    """
    
    def initialize(self):
        """Initialize the advanced trading agent with all components."""
        # Core configuration
        self.expiry_period = int(getattr(self.config, 'expiry_period', 120_000_000_000))  # 2 minutes
        self.max_position_size = float(getattr(self.config, 'max_position_size', 10.0))
        self.risk_tolerance = float(getattr(self.config, 'risk_tolerance', 0.02))  # 2% max risk per trade
        self.max_drawdown = float(getattr(self.config, 'max_drawdown', 0.1))  # 10% max drawdown
        
        # Strategy parameters
        self.strategies = {
            'momentum': float(getattr(self.config, 'momentum_weight', 0.3)),
            'mean_reversion': float(getattr(self.config, 'mean_reversion_weight', 0.3)),
            'arbitrage': float(getattr(self.config, 'arbitrage_weight', 0.2)),
            'ml_signal': float(getattr(self.config, 'ml_signal_weight', 0.2))
        }
        
        # ML configuration
        self.ml_model_type = getattr(self.config, 'ml_model', 'ensemble')
        self.feature_window = int(getattr(self.config, 'feature_window', 20))
        self.retrain_interval = int(getattr(self.config, 'retrain_interval', 100))
        self.min_training_samples = int(getattr(self.config, 'min_training_samples', 50))
        
        # Performance tracking
        self.performance_metrics = defaultdict(lambda: {
            'returns': deque(maxlen=1000),
            'trades': deque(maxlen=1000),
            'sharpe_ratio': 0.0,
            'max_drawdown': 0.0,
            'win_rate': 0.0,
            'profit_factor': 0.0
        })
        
        # Initialize components
        self.history_manager = StateHistoryManager(
            history_retention_mins=getattr(self.config, 'history_retention_mins', 30),
            log_dir=self.log_dir,
            parallel_workers=getattr(self.config, 'parallel_history_workers', 4)
        )
        
        # ML models and scalers
        self.models = defaultdict(dict)
        self.scalers = defaultdict(dict)
        self.feature_importance = defaultdict(dict)
        
        # Trading state
        self.active_orders = defaultdict(list)
        self.position_sizes = defaultdict(float)
        self.last_prices = defaultdict(float)
        self.volatility_estimates = defaultdict(float)
        
        # Risk management
        self.var_estimates = defaultdict(float)
        self.correlation_matrix = defaultdict(dict)
        self.portfolio_value = defaultdict(float)
        
        bt.logging.info(f"Advanced Trading Agent initialized with strategies: {self.strategies}")
    
    def calculate_features(self, book: Book, timestamp: int, validator: str) -> Dict[str, float]:
        """Calculate comprehensive features for ML model."""
        features = {}
        
        if not book.bids or not book.asks:
            return features
            
        # Basic price features
        best_bid = book.bids[0].price
        best_ask = book.asks[0].price
        mid_price = (best_bid + best_ask) / 2
        spread = best_ask - best_bid
        spread_pct = spread / mid_price if mid_price > 0 else 0
        
        features['mid_price'] = mid_price
        features['spread'] = spread
        features['spread_pct'] = spread_pct
        
        # Order book imbalance
        bid_volume = sum(level.quantity for level in book.bids[:5])
        ask_volume = sum(level.quantity for level in book.asks[:5])
        imbalance = (bid_volume - ask_volume) / (bid_volume + ask_volume) if (bid_volume + ask_volume) > 0 else 0
        features['imbalance'] = imbalance
        
        # Price momentum
        if validator in self.last_prices and book.id in self.last_prices[validator]:
            price_change = mid_price - self.last_prices[validator][book.id]
            features['price_change'] = price_change
            features['price_change_pct'] = price_change / self.last_prices[validator][book.id] if self.last_prices[validator][book.id] > 0 else 0
        else:
            features['price_change'] = 0.0
            features['price_change_pct'] = 0.0
        
        # Volatility estimation
        if validator in self.performance_metrics and book.id in self.performance_metrics[validator]:
            returns = list(self.performance_metrics[validator][book.id]['returns'])
            if len(returns) > 5:
                volatility = np.std(returns[-20:]) if len(returns) >= 20 else np.std(returns)
                features['volatility'] = volatility
                self.volatility_estimates[validator][book.id] = volatility
            else:
                features['volatility'] = 0.01  # Default volatility
        else:
            features['volatility'] = 0.01
        
        # Volume features
        if book.events:
            recent_trades = [event for event in book.events if hasattr(event, 'type') and event.type == 't']
            if recent_trades:
                total_volume = sum(trade.quantity for trade in recent_trades[-10:])  # Last 10 trades
                features['recent_volume'] = total_volume
                features['avg_trade_size'] = total_volume / len(recent_trades[-10:])
            else:
                features['recent_volume'] = 0.0
                features['avg_trade_size'] = 0.0
        else:
            features['recent_volume'] = 0.0
            features['avg_trade_size'] = 0.0
        
        # Technical indicators
        if validator in self.history_manager and book.id in self.history_manager[validator]:
            history = self.history_manager[validator][book.id]
            if history.is_full():
                # Moving averages
                prices = [snapshot.mid_price for snapshot in history.snapshots[-20:]]
                if len(prices) >= 5:
                    features['sma_5'] = np.mean(prices[-5:])
                    features['sma_10'] = np.mean(prices[-10:]) if len(prices) >= 10 else np.mean(prices)
                    features['sma_20'] = np.mean(prices)
                    
                    # RSI-like momentum
                    if len(prices) >= 14:
                        gains = [max(0, prices[i] - prices[i-1]) for i in range(1, len(prices))]
                        losses = [max(0, prices[i-1] - prices[i]) for i in range(1, len(prices))]
                        avg_gain = np.mean(gains[-14:])
                        avg_loss = np.mean(losses[-14:])
                        if avg_loss > 0:
                            rs = avg_gain / avg_loss
                            features['rsi'] = 100 - (100 / (1 + rs))
                        else:
                            features['rsi'] = 100
                    else:
                        features['rsi'] = 50
                else:
                    features['sma_5'] = mid_price
                    features['sma_10'] = mid_price
                    features['sma_20'] = mid_price
                    features['rsi'] = 50
        
        # Market microstructure features
        features['bid_ask_spread_ratio'] = spread / mid_price if mid_price > 0 else 0
        features['order_book_pressure'] = imbalance * spread_pct
        
        return features
    
    def generate_ml_signal(self, features: Dict[str, float], validator: str, book_id: int) -> TradingSignal:
        """Generate ML-based trading signal."""
        if validator not in self.models or book_id not in self.models[validator]:
            return TradingSignal(0, 0.0, 0.0, 1.0, 0.0)
        
        try:
            model = self.models[validator][book_id]
            scaler = self.scalers[validator][book_id]
            
            # Prepare features
            feature_vector = np.array([features.get(key, 0.0) for key in self.get_feature_names()]).reshape(1, -1)
            scaled_features = scaler.transform(feature_vector)
            
            # Make prediction
            prediction = model.predict(scaled_features)[0]
            confidence = min(abs(prediction) * 10, 1.0)  # Scale confidence
            
            # Risk assessment
            risk_score = min(features.get('volatility', 0.01) * 50, 1.0)
            
            # Generate signal
            if abs(prediction) > 0.001:  # Minimum threshold
                direction = 1 if prediction > 0 else -1
                strength = min(abs(prediction) * 2, 1.0)
                expected_return = prediction
            else:
                direction = 0
                strength = 0.0
                expected_return = 0.0
            
            return TradingSignal(
                direction=direction,
                strength=strength,
                confidence=confidence,
                risk_score=risk_score,
                expected_return=expected_return
            )
            
        except Exception as e:
            bt.logging.error(f"ML signal generation failed: {e}")
            return TradingSignal(0, 0.0, 0.0, 1.0, 0.0)
    
    def generate_momentum_signal(self, features: Dict[str, float]) -> TradingSignal:
        """Generate momentum-based trading signal."""
        price_change_pct = features.get('price_change_pct', 0.0)
        volatility = features.get('volatility', 0.01)
        
        # Momentum signal based on price change and volatility
        if abs(price_change_pct) > volatility * 2:  # Significant move
            direction = 1 if price_change_pct > 0 else -1
            strength = min(abs(price_change_pct) / (volatility * 4), 1.0)
            confidence = min(abs(price_change_pct) / volatility, 1.0)
        else:
            direction = 0
            strength = 0.0
            confidence = 0.0
        
        return TradingSignal(
            direction=direction,
            strength=strength,
            confidence=confidence,
            risk_score=volatility,
            expected_return=price_change_pct
        )
    
    def generate_mean_reversion_signal(self, features: Dict[str, float]) -> TradingSignal:
        """Generate mean reversion trading signal."""
        rsi = features.get('rsi', 50)
        mid_price = features.get('mid_price', 0)
        sma_20 = features.get('sma_20', mid_price)
        
        # Mean reversion based on RSI and price deviation from SMA
        price_deviation = (mid_price - sma_20) / sma_20 if sma_20 > 0 else 0
        
        if rsi > 70 and price_deviation > 0.01:  # Overbought
            direction = -1
            strength = min((rsi - 70) / 30, 1.0)
            confidence = min(abs(price_deviation) * 100, 1.0)
        elif rsi < 30 and price_deviation < -0.01:  # Oversold
            direction = 1
            strength = min((30 - rsi) / 30, 1.0)
            confidence = min(abs(price_deviation) * 100, 1.0)
        else:
            direction = 0
            strength = 0.0
            confidence = 0.0
        
        return TradingSignal(
            direction=direction,
            strength=strength,
            confidence=confidence,
            risk_score=features.get('volatility', 0.01),
            expected_return=-price_deviation  # Mean reversion expects opposite of current trend
        )
    
    def generate_arbitrage_signal(self, features: Dict[str, float]) -> TradingSignal:
        """Generate arbitrage trading signal based on order book imbalances."""
        imbalance = features.get('imbalance', 0.0)
        spread_pct = features.get('spread_pct', 0.0)
        
        # Arbitrage signal based on order book imbalance
        if abs(imbalance) > 0.3 and spread_pct > 0.001:  # Significant imbalance with spread
            direction = 1 if imbalance > 0 else -1
            strength = min(abs(imbalance), 1.0)
            confidence = min(abs(imbalance) * 2, 1.0)
        else:
            direction = 0
            strength = 0.0
            confidence = 0.0
        
        return TradingSignal(
            direction=direction,
            strength=strength,
            confidence=confidence,
            risk_score=spread_pct,
            expected_return=imbalance * 0.001  # Small expected return from arbitrage
        )
    
    def combine_signals(self, signals: Dict[str, TradingSignal]) -> TradingSignal:
        """Combine multiple trading signals with weighted averaging."""
        if not signals:
            return TradingSignal(0, 0.0, 0.0, 1.0, 0.0)
        
        # Weighted combination
        total_weight = sum(self.strategies.get(name, 0.0) for name in signals.keys())
        if total_weight == 0:
            return TradingSignal(0, 0.0, 0.0, 1.0, 0.0)
        
        weighted_direction = sum(
            signal.direction * self.strategies.get(name, 0.0) 
            for name, signal in signals.items()
        ) / total_weight
        
        weighted_strength = sum(
            signal.strength * self.strategies.get(name, 0.0) 
            for name, signal in signals.items()
        ) / total_weight
        
        weighted_confidence = sum(
            signal.confidence * self.strategies.get(name, 0.0) 
            for name, signal in signals.items()
        ) / total_weight
        
        weighted_risk = sum(
            signal.risk_score * self.strategies.get(name, 0.0) 
            for name, signal in signals.items()
        ) / total_weight
        
        weighted_return = sum(
            signal.expected_return * self.strategies.get(name, 0.0) 
            for name, signal in signals.items()
        ) / total_weight
        
        # Final signal
        final_direction = 1 if weighted_direction > 0.3 else (-1 if weighted_direction < -0.3 else 0)
        final_strength = min(weighted_strength, 1.0)
        final_confidence = min(weighted_confidence, 1.0)
        
        return TradingSignal(
            direction=final_direction,
            strength=final_strength,
            confidence=final_confidence,
            risk_score=weighted_risk,
            expected_return=weighted_return
        )
    
    def calculate_position_size(self, signal: TradingSignal, book: Book, validator: str, book_id: int) -> float:
        """Calculate position size based on signal strength, risk, and available capital."""
        if signal.direction == 0 or signal.strength == 0:
            return 0.0
        
        # Base position size
        base_size = self.max_position_size * signal.strength
        
        # Risk adjustment
        risk_adjustment = 1.0 - signal.risk_score
        risk_adjusted_size = base_size * risk_adjustment
        
        # Confidence adjustment
        confidence_adjustment = signal.confidence
        final_size = risk_adjusted_size * confidence_adjustment
        
        # Account for available capital
        if validator in self.portfolio_value and book_id in self.portfolio_value[validator]:
            available_capital = self.portfolio_value[validator][book_id]
            max_affordable = available_capital * self.risk_tolerance
            final_size = min(final_size, max_affordable)
        
        # Minimum size check
        if final_size < 0.1:  # Minimum trade size
            return 0.0
        
        return round(final_size, 2)
    
    def train_ml_model(self, validator: str, book_id: int):
        """Train ML model with historical data."""
        try:
            if validator not in self.performance_metrics or book_id not in self.performance_metrics[validator]:
                return
            
            # Collect training data
            X, y = [], []
            returns = list(self.performance_metrics[validator][book_id]['returns'])
            
            if len(returns) < self.min_training_samples:
                return
            
            # Prepare features and targets
            for i in range(self.feature_window, len(returns)):
                # Use historical features (simplified for this example)
                features = {
                    'volatility': np.std(returns[i-self.feature_window:i]),
                    'momentum': np.mean(returns[i-5:i]) if i >= 5 else 0,
                    'mean_reversion': returns[i-1] - np.mean(returns[i-self.feature_window:i-1])
                }
                
                X.append([features.get(key, 0.0) for key in self.get_feature_names()])
                y.append(returns[i])
            
            if len(X) < self.min_training_samples:
                return
            
            X, y = np.array(X), np.array(y)
            
            # Train model
            if self.ml_model_type == 'ensemble':
                model = GradientBoostingRegressor(n_estimators=50, max_depth=3, random_state=42)
            elif self.ml_model_type == 'ridge':
                model = Ridge(alpha=1.0)
            elif self.ml_model_type == 'lasso':
                model = Lasso(alpha=0.01)
            else:
                model = RandomForestRegressor(n_estimators=50, max_depth=3, random_state=42)
            
            # Scale features
            scaler = StandardScaler()
            X_scaled = scaler.fit_transform(X)
            
            # Train
            model.fit(X_scaled, y)
            
            # Store model and scaler
            self.models[validator][book_id] = model
            self.scalers[validator][book_id] = scaler
            
            # Calculate feature importance
            if hasattr(model, 'feature_importances_'):
                self.feature_importance[validator][book_id] = dict(zip(
                    self.get_feature_names(), model.feature_importances_
                ))
            
            bt.logging.info(f"ML model trained for validator {validator}, book {book_id}")
            
        except Exception as e:
            bt.logging.error(f"ML model training failed: {e}")
    
    def get_feature_names(self) -> List[str]:
        """Get list of feature names for ML model."""
        return [
            'mid_price', 'spread', 'spread_pct', 'imbalance', 'price_change', 'price_change_pct',
            'volatility', 'recent_volume', 'avg_trade_size', 'sma_5', 'sma_10', 'sma_20',
            'rsi', 'bid_ask_spread_ratio', 'order_book_pressure'
        ]
    
    def update_performance_metrics(self, validator: str, book_id: int, return_value: float, trade_pnl: float):
        """Update performance metrics for the agent."""
        if validator not in self.performance_metrics:
            self.performance_metrics[validator] = {}
        if book_id not in self.performance_metrics[validator]:
            self.performance_metrics[validator][book_id] = {
                'returns': deque(maxlen=1000),
                'trades': deque(maxlen=1000),
                'sharpe_ratio': 0.0,
                'max_drawdown': 0.0,
                'win_rate': 0.0,
                'profit_factor': 0.0
            }
        
        metrics = self.performance_metrics[validator][book_id]
        metrics['returns'].append(return_value)
        metrics['trades'].append(trade_pnl)
        
        # Calculate Sharpe ratio
        if len(metrics['returns']) > 10:
            returns_array = np.array(list(metrics['returns']))
            sharpe = np.mean(returns_array) / np.std(returns_array) if np.std(returns_array) > 0 else 0
            metrics['sharpe_ratio'] = sharpe
        
        # Calculate win rate
        if len(metrics['trades']) > 0:
            winning_trades = sum(1 for pnl in metrics['trades'] if pnl > 0)
            metrics['win_rate'] = winning_trades / len(metrics['trades'])
        
        # Calculate profit factor
        if len(metrics['trades']) > 0:
            profits = sum(pnl for pnl in metrics['trades'] if pnl > 0)
            losses = abs(sum(pnl for pnl in metrics['trades'] if pnl < 0))
            metrics['profit_factor'] = profits / losses if losses > 0 else float('inf')
    
    def respond(self, state: MarketSimulationStateUpdate) -> FinanceAgentResponse:
        """Main response method that processes market state and generates trading instructions."""
        response = FinanceAgentResponse(agent_id=self.uid)
        validator = state.dendrite.hotkey
        
        # Wait for history update to complete
        while self.history_manager.updating:
            time.sleep(0.1)
        
        for book_id, book in state.books.items():
            try:
                if not book.bids or not book.asks:
                    continue
                
                # Get basic price info first
                best_bid = book.bids[0].price
                best_ask = book.asks[0].price
                mid_price = (best_bid + best_ask) / 2
                
                # Calculate features
                features = self.calculate_features(book, state.timestamp, validator)
                if not features:
                    # Still need to update last price even if no features
                    if validator not in self.last_prices:
                        self.last_prices[validator] = {}
                    self.last_prices[validator][book_id] = mid_price
                    continue
                
                # Generate signals from different strategies
                signals = {}
                
                # Momentum signal
                signals['momentum'] = self.generate_momentum_signal(features)
                
                # Mean reversion signal
                signals['mean_reversion'] = self.generate_mean_reversion_signal(features)
                
                # Arbitrage signal
                signals['arbitrage'] = self.generate_arbitrage_signal(features)
                
                # ML signal
                if validator in self.models and book_id in self.models[validator]:
                    signals['ml_signal'] = self.generate_ml_signal(features, validator, book_id)
                
                # Combine signals
                final_signal = self.combine_signals(signals)
                
                # Calculate position size
                position_size = self.calculate_position_size(final_signal, book, validator, book_id)
                
                if position_size > 0 and final_signal.direction != 0:
                    # Place orders based on signal
                    
                    if final_signal.direction == 1:  # Buy signal
                        # Place buy order slightly above best bid
                        buy_price = round(best_bid + 10**(-state.config.priceDecimals), state.config.priceDecimals)
                        response.limit_order(
                            book_id=book_id,
                            direction=OrderDirection.BUY,
                            quantity=position_size,
                            price=buy_price,
                            timeInForce=TimeInForce.GTT,
                            expiryPeriod=self.expiry_period,
                            stp=STP.CANCEL_BOTH
                        )
                        
                        # Place sell order for profit taking
                        sell_price = round(mid_price * (1 + final_signal.expected_return * 2), state.config.priceDecimals)
                        response.limit_order(
                            book_id=book_id,
                            direction=OrderDirection.SELL,
                            quantity=position_size,
                            price=sell_price,
                            timeInForce=TimeInForce.GTT,
                            expiryPeriod=self.expiry_period,
                            stp=STP.CANCEL_BOTH
                        )
                    
                    elif final_signal.direction == -1:  # Sell signal
                        # Place sell order slightly below best ask
                        sell_price = round(best_ask - 10**(-state.config.priceDecimals), state.config.priceDecimals)
                        response.limit_order(
                            book_id=book_id,
                            direction=OrderDirection.SELL,
                            quantity=position_size,
                            price=sell_price,
                            timeInForce=TimeInForce.GTT,
                            expiryPeriod=self.expiry_period,
                            stp=STP.CANCEL_BOTH
                        )
                        
                        # Place buy order for profit taking
                        buy_price = round(mid_price * (1 - final_signal.expected_return * 2), state.config.priceDecimals)
                        response.limit_order(
                            book_id=book_id,
                            direction=OrderDirection.BUY,
                            quantity=position_size,
                            price=buy_price,
                            timeInForce=TimeInForce.GTT,
                            expiryPeriod=self.expiry_period,
                            stp=STP.CANCEL_BOTH
                        )
                
                # Update performance metrics
                if validator in self.last_prices and book_id in self.last_prices[validator]:
                    price_change = mid_price - self.last_prices[validator][book_id]
                    return_pct = price_change / self.last_prices[validator][book_id] if self.last_prices[validator][book_id] > 0 else 0
                    self.update_performance_metrics(validator, book_id, return_pct, 0.0)
                
                # Update last price
                if validator not in self.last_prices:
                    self.last_prices[validator] = {}
                self.last_prices[validator][book_id] = mid_price
                
                # Train ML model periodically
                if (validator in self.performance_metrics and 
                    book_id in self.performance_metrics[validator] and
                    len(self.performance_metrics[validator][book_id]['returns']) % self.retrain_interval == 0):
                    Thread(target=self.train_ml_model, args=(validator, book_id)).start()
                
                # Log performance
                if validator in self.performance_metrics and book_id in self.performance_metrics[validator]:
                    metrics = self.performance_metrics[validator][book_id]
                    bt.logging.info(
                        f"BOOK {book_id}: Signal={final_signal.direction}, "
                        f"Strength={final_signal.strength:.3f}, "
                        f"Confidence={final_signal.confidence:.3f}, "
                        f"Sharpe={metrics['sharpe_ratio']:.3f}, "
                        f"WinRate={metrics['win_rate']:.3f}"
                    )
                
            except Exception as e:
                bt.logging.error(f"Error processing book {book_id}: {e}")
                continue
        
        # Update history asynchronously
        self.history_manager.update_async(state.model_copy(deep=True))
        
        return response

if __name__ == "__main__":
    """
    Example command for local standalone execution:
    python advanced_trading_agent.py --port 8888 --agent_id 0 --params \
        expiry_period=120000000000 \
        max_position_size=10.0 \
        risk_tolerance=0.02 \
        momentum_weight=0.3 \
        mean_reversion_weight=0.3 \
        arbitrage_weight=0.2 \
        ml_signal_weight=0.2 \
        ml_model=ensemble \
        feature_window=20 \
        retrain_interval=100
    """
    launch(AdvancedTradingAgent)
