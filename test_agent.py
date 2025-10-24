#!/usr/bin/env python3
"""
Test script for the Advanced Trading Agent
This script allows you to test the agent locally before deploying to the subnet.
"""

import sys
import os
import time
import json
import numpy as np
import pandas as pd
from typing import Dict, List
import logging

# Add the current directory to Python path
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

def create_mock_state_update():
    """Create a mock state update for testing purposes."""
    from taos.im.protocol.models import *
    from taos.im.protocol import MarketSimulationStateUpdate
    
    # Create mock book data
    book_id = 1
    timestamp = int(time.time() * 1_000_000_000)  # Current time in nanoseconds
    
    # Create mock order book levels
    bids = []
    asks = []
    
    base_price = 300.0
    for i in range(21):  # 21 levels
        bid_price = base_price - (i * 0.01)
        ask_price = base_price + (i * 0.01)
        
        bids.append(LevelInfo(
            price=bid_price,
            quantity=100.0 + np.random.uniform(0, 50),
            orders=None
        ))
        asks.append(LevelInfo(
            price=ask_price,
            quantity=100.0 + np.random.uniform(0, 50),
            orders=None
        ))
    
    # Create mock book
    book = Book(
        id=book_id,
        bids=bids,
        asks=asks,
        events=[]
    )
    
    # Create mock config
    config = MarketSimulationConfig(
        baseDecimals=8,
        quoteDecimals=8,
        priceDecimals=2,
        volumeDecimals=2,
        fee_policy="maker_taker",
        max_open_orders=10,
        miner_wealth=10000.0,
        publish_interval=1_000_000_000  # 1 second
    )
    
    # Create mock accounts
    accounts = {
        1: {  # Agent ID
            book_id: Account(
                agent_id=1,
                book_id=book_id,
                base_balance=Balance(currency="BTC", total=10.0, free=10.0, reserved=0.0),
                quote_balance=Balance(currency="USD", total=3000.0, free=3000.0, reserved=0.0),
                orders=[],
                fees=None
            )
        }
    }
    
    # Create mock state update
    state = MarketSimulationStateUpdate(
        version=1,
        timestamp=timestamp,
        config=config,
        books={book_id: book},
        accounts=accounts,
        notices={},
        response=None,
        compressed=None,
        compression_engine="lz4"
    )
    
    return state

def test_agent_initialization():
    """Test agent initialization."""
    logger.info("Testing agent initialization...")
    
    try:
        from advanced_trading_agent import AdvancedTradingAgent
        
        # Create mock config
        class MockConfig:
            def __init__(self):
                self.expiry_period = 120_000_000_000
                self.max_position_size = 10.0
                self.risk_tolerance = 0.02
                self.max_drawdown = 0.1
                self.momentum_weight = 0.3
                self.mean_reversion_weight = 0.3
                self.arbitrage_weight = 0.2
                self.ml_signal_weight = 0.2
                self.ml_model = 'ensemble'
                self.feature_window = 20
                self.retrain_interval = 100
                self.min_training_samples = 50
                self.history_retention_mins = 30
                self.parallel_history_workers = 4
        
        # Create agent
        agent = AdvancedTradingAgent()
        agent.config = MockConfig()
        agent.uid = 1
        agent.log_dir = "./test_logs"
        
        # Initialize agent
        agent.initialize()
        
        logger.info("✅ Agent initialization successful")
        return True
        
    except Exception as e:
        logger.error(f"❌ Agent initialization failed: {e}")
        return False

def test_feature_calculation():
    """Test feature calculation."""
    logger.info("Testing feature calculation...")
    
    try:
        from advanced_trading_agent import AdvancedTradingAgent
        from taos.im.protocol.models import Book, LevelInfo
        
        # Create agent
        agent = AdvancedTradingAgent()
        agent.config = type('Config', (), {
            'expiry_period': 120_000_000_000,
            'max_position_size': 10.0,
            'risk_tolerance': 0.02,
            'max_drawdown': 0.1,
            'momentum_weight': 0.3,
            'mean_reversion_weight': 0.3,
            'arbitrage_weight': 0.2,
            'ml_signal_weight': 0.2,
            'ml_model': 'ensemble',
            'feature_window': 20,
            'retrain_interval': 100,
            'min_training_samples': 50,
            'history_retention_mins': 30,
            'parallel_history_workers': 4
        })()
        agent.uid = 1
        agent.log_dir = "./test_logs"
        agent.initialize()
        
        # Create mock book
        bids = [LevelInfo(price=300.0 - i*0.01, quantity=100.0, orders=None) for i in range(21)]
        asks = [LevelInfo(price=300.0 + i*0.01, quantity=100.0, orders=None) for i in range(21)]
        book = Book(id=1, bids=bids, asks=asks, events=[])
        
        # Test feature calculation
        features = agent.calculate_features(book, int(time.time() * 1_000_000_000), "test_validator")
        
        logger.info(f"✅ Feature calculation successful. Features: {len(features)}")
        logger.info(f"Sample features: {dict(list(features.items())[:5])}")
        return True
        
    except Exception as e:
        logger.error(f"❌ Feature calculation failed: {e}")
        return False

def test_signal_generation():
    """Test signal generation."""
    logger.info("Testing signal generation...")
    
    try:
        from advanced_trading_agent import AdvancedTradingAgent
        
        # Create agent
        agent = AdvancedTradingAgent()
        agent.config = type('Config', (), {
            'expiry_period': 120_000_000_000,
            'max_position_size': 10.0,
            'risk_tolerance': 0.02,
            'max_drawdown': 0.1,
            'momentum_weight': 0.3,
            'mean_reversion_weight': 0.3,
            'arbitrage_weight': 0.2,
            'ml_signal_weight': 0.2,
            'ml_model': 'ensemble',
            'feature_window': 20,
            'retrain_interval': 100,
            'min_training_samples': 50,
            'history_retention_mins': 30,
            'parallel_history_workers': 4
        })()
        agent.uid = 1
        agent.log_dir = "./test_logs"
        agent.initialize()
        
        # Test features
        features = {
            'mid_price': 300.0,
            'spread': 0.01,
            'spread_pct': 0.000033,
            'imbalance': 0.1,
            'price_change_pct': 0.01,
            'volatility': 0.02,
            'rsi': 60.0,
            'sma_20': 299.5
        }
        
        # Test momentum signal
        momentum_signal = agent.generate_momentum_signal(features)
        logger.info(f"Momentum signal: direction={momentum_signal.direction}, strength={momentum_signal.strength:.3f}")
        
        # Test mean reversion signal
        mean_reversion_signal = agent.generate_mean_reversion_signal(features)
        logger.info(f"Mean reversion signal: direction={mean_reversion_signal.direction}, strength={mean_reversion_signal.strength:.3f}")
        
        # Test arbitrage signal
        arbitrage_signal = agent.generate_arbitrage_signal(features)
        logger.info(f"Arbitrage signal: direction={arbitrage_signal.direction}, strength={arbitrage_signal.strength:.3f}")
        
        logger.info("✅ Signal generation successful")
        return True
        
    except Exception as e:
        logger.error(f"❌ Signal generation failed: {e}")
        return False

def test_full_agent_response():
    """Test full agent response."""
    logger.info("Testing full agent response...")
    
    try:
        from advanced_trading_agent import AdvancedTradingAgent
        
        # Create agent
        agent = AdvancedTradingAgent()
        agent.config = type('Config', (), {
            'expiry_period': 120_000_000_000,
            'max_position_size': 10.0,
            'risk_tolerance': 0.02,
            'max_drawdown': 0.1,
            'momentum_weight': 0.3,
            'mean_reversion_weight': 0.3,
            'arbitrage_weight': 0.2,
            'ml_signal_weight': 0.2,
            'ml_model': 'ensemble',
            'feature_window': 20,
            'retrain_interval': 100,
            'min_training_samples': 50,
            'history_retention_mins': 30,
            'parallel_history_workers': 4
        })()
        agent.uid = 1
        agent.log_dir = "./test_logs"
        agent.initialize()
        
        # Create mock state
        state = create_mock_state_update()
        
        # Test agent response
        response = agent.respond(state)
        
        logger.info(f"✅ Agent response successful. Instructions: {len(response.instructions)}")
        for i, instruction in enumerate(response.instructions):
            logger.info(f"Instruction {i+1}: {type(instruction).__name__}")
        
        return True
        
    except Exception as e:
        logger.error(f"❌ Agent response failed: {e}")
        return False

def run_performance_test():
    """Run performance test to measure response time."""
    logger.info("Running performance test...")
    
    try:
        from advanced_trading_agent import AdvancedTradingAgent
        
        # Create agent
        agent = AdvancedTradingAgent()
        agent.config = type('Config', (), {
            'expiry_period': 120_000_000_000,
            'max_position_size': 10.0,
            'risk_tolerance': 0.02,
            'max_drawdown': 0.1,
            'momentum_weight': 0.3,
            'mean_reversion_weight': 0.3,
            'arbitrage_weight': 0.2,
            'ml_signal_weight': 0.2,
            'ml_model': 'ensemble',
            'feature_window': 20,
            'retrain_interval': 100,
            'min_training_samples': 50,
            'history_retention_mins': 30,
            'parallel_history_workers': 4
        })()
        agent.uid = 1
        agent.log_dir = "./test_logs"
        agent.initialize()
        
        # Create mock state
        state = create_mock_state_update()
        
        # Measure response time
        start_time = time.time()
        response = agent.respond(state)
        end_time = time.time()
        
        response_time = end_time - start_time
        logger.info(f"✅ Performance test successful. Response time: {response_time:.3f}s")
        
        if response_time > 3.0:  # Subnet timeout is typically 3 seconds
            logger.warning(f"⚠️  Response time ({response_time:.3f}s) is close to timeout limit")
        
        return True
        
    except Exception as e:
        logger.error(f"❌ Performance test failed: {e}")
        return False

def main():
    """Run all tests."""
    logger.info("🚀 Starting Advanced Trading Agent Tests")
    logger.info("=" * 50)
    
    # Create test directory
    os.makedirs("./test_logs", exist_ok=True)
    
    tests = [
        ("Agent Initialization", test_agent_initialization),
        ("Feature Calculation", test_feature_calculation),
        ("Signal Generation", test_signal_generation),
        ("Full Agent Response", test_full_agent_response),
        ("Performance Test", run_performance_test)
    ]
    
    results = []
    
    for test_name, test_func in tests:
        logger.info(f"\n🧪 Running {test_name}...")
        try:
            result = test_func()
            results.append((test_name, result))
        except Exception as e:
            logger.error(f"❌ {test_name} failed with exception: {e}")
            results.append((test_name, False))
    
    # Summary
    logger.info("\n" + "=" * 50)
    logger.info("📊 Test Results Summary")
    logger.info("=" * 50)
    
    passed = 0
    total = len(results)
    
    for test_name, result in results:
        status = "✅ PASSED" if result else "❌ FAILED"
        logger.info(f"{test_name}: {status}")
        if result:
            passed += 1
    
    logger.info(f"\nOverall: {passed}/{total} tests passed")
    
    if passed == total:
        logger.info("🎉 All tests passed! Your agent is ready for deployment.")
        logger.info("\nNext steps:")
        logger.info("1. Run: chmod +x deploy_miner.sh")
        logger.info("2. Run: ./deploy_miner.sh")
        logger.info("3. Monitor with: pm2 logs miner")
    else:
        logger.error("❌ Some tests failed. Please fix the issues before deploying.")
    
    return passed == total

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
