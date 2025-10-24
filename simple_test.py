#!/usr/bin/env python3
"""
Simple test script for the Advanced Trading Agent
This script tests the basic functionality without complex imports.
"""

import sys
import os
import time
import numpy as np
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

def test_basic_functionality():
    """Test basic Python functionality and imports."""
    logger.info("🧪 Testing basic functionality...")
    
    try:
        # Test numpy
        arr = np.array([1, 2, 3, 4, 5])
        mean_val = np.mean(arr)
        logger.info(f"✅ NumPy test passed. Mean: {mean_val}")
        
        # Test pandas
        import pandas as pd
        df = pd.DataFrame({'a': [1, 2, 3], 'b': [4, 5, 6]})
        logger.info(f"✅ Pandas test passed. DataFrame shape: {df.shape}")
        
        # Test scikit-learn
        from sklearn.ensemble import RandomForestRegressor
        from sklearn.preprocessing import StandardScaler
        
        # Create simple model
        X = np.random.rand(100, 5)
        y = np.random.rand(100)
        
        scaler = StandardScaler()
        X_scaled = scaler.fit_transform(X)
        
        model = RandomForestRegressor(n_estimators=10, random_state=42)
        model.fit(X_scaled, y)
        
        predictions = model.predict(X_scaled[:5])
        logger.info(f"✅ Scikit-learn test passed. Predictions: {predictions[:3]}")
        
        return True
        
    except Exception as e:
        logger.error(f"❌ Basic functionality test failed: {e}")
        return False

def test_agent_import():
    """Test if the advanced trading agent can be imported."""
    logger.info("🧪 Testing agent import...")
    
    try:
        # Add current directory to path
        current_dir = os.path.dirname(os.path.abspath(__file__))
        if current_dir not in sys.path:
            sys.path.insert(0, current_dir)
        
        # Try to import the agent
        from advanced_trading_agent import AdvancedTradingAgent
        logger.info("✅ Advanced Trading Agent import successful")
        
        # Test basic instantiation
        agent = AdvancedTradingAgent()
        logger.info("✅ Agent instantiation successful")
        
        return True
        
    except ImportError as e:
        logger.error(f"❌ Import error: {e}")
        logger.info("💡 Make sure advanced_trading_agent.py is in the same directory")
        return False
    except Exception as e:
        logger.error(f"❌ Agent import test failed: {e}")
        return False

def test_agent_initialization():
    """Test agent initialization with mock config."""
    logger.info("🧪 Testing agent initialization...")
    
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
        logger.info(f"   - Max position size: {agent.max_position_size}")
        logger.info(f"   - Risk tolerance: {agent.risk_tolerance}")
        logger.info(f"   - Strategy weights: {agent.strategies}")
        
        return True
        
    except Exception as e:
        logger.error(f"❌ Agent initialization failed: {e}")
        return False

def test_feature_calculation():
    """Test feature calculation with mock data."""
    logger.info("🧪 Testing feature calculation...")
    
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
        
        # Create mock book data
        class MockLevelInfo:
            def __init__(self, price, quantity):
                self.price = price
                self.quantity = quantity
                self.orders = None
        
        class MockBook:
            def __init__(self, book_id):
                self.id = book_id
                self.bids = [MockLevelInfo(300.0 - i*0.01, 100.0) for i in range(21)]
                self.asks = [MockLevelInfo(300.0 + i*0.01, 100.0) for i in range(21)]
                self.events = []
        
        # Test feature calculation
        book = MockBook(1)
        features = agent.calculate_features(book, int(time.time() * 1_000_000_000), "test_validator")
        
        logger.info(f"✅ Feature calculation successful. Features: {len(features)}")
        logger.info(f"   - Sample features: {dict(list(features.items())[:3])}")
        
        return True
        
    except Exception as e:
        logger.error(f"❌ Feature calculation failed: {e}")
        return False

def test_signal_generation():
    """Test signal generation methods."""
    logger.info("🧪 Testing signal generation...")
    
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
        logger.info(f"✅ Momentum signal: direction={momentum_signal.direction}, strength={momentum_signal.strength:.3f}")
        
        # Test mean reversion signal
        mean_reversion_signal = agent.generate_mean_reversion_signal(features)
        logger.info(f"✅ Mean reversion signal: direction={mean_reversion_signal.direction}, strength={mean_reversion_signal.strength:.3f}")
        
        # Test arbitrage signal
        arbitrage_signal = agent.generate_arbitrage_signal(features)
        logger.info(f"✅ Arbitrage signal: direction={arbitrage_signal.direction}, strength={arbitrage_signal.strength:.3f}")
        
        return True
        
    except Exception as e:
        logger.error(f"❌ Signal generation failed: {e}")
        return False

def run_performance_test():
    """Run a simple performance test."""
    logger.info("🧪 Running performance test...")
    
    try:
        # Test basic operations speed
        start_time = time.time()
        
        # Simulate some computation
        data = np.random.rand(1000, 10)
        result = np.mean(data, axis=1)
        processed = np.std(result)
        
        end_time = time.time()
        computation_time = end_time - start_time
        
        logger.info(f"✅ Performance test successful. Computation time: {computation_time:.3f}s")
        
        if computation_time > 1.0:
            logger.warning(f"⚠️  Computation time ({computation_time:.3f}s) is slower than expected")
        
        return True
        
    except Exception as e:
        logger.error(f"❌ Performance test failed: {e}")
        return False

def main():
    """Run all tests."""
    logger.info("🚀 Starting Simple Advanced Trading Agent Tests")
    logger.info("=" * 60)
    
    # Create test directory
    os.makedirs("./test_logs", exist_ok=True)
    
    tests = [
        ("Basic Functionality", test_basic_functionality),
        ("Agent Import", test_agent_import),
        ("Agent Initialization", test_agent_initialization),
        ("Feature Calculation", test_feature_calculation),
        ("Signal Generation", test_signal_generation),
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
    logger.info("\n" + "=" * 60)
    logger.info("📊 Test Results Summary")
    logger.info("=" * 60)
    
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
        logger.info("\nTroubleshooting:")
        logger.info("- Make sure all dependencies are installed: pip install numpy pandas scikit-learn bittensor")
        logger.info("- Check that advanced_trading_agent.py is in the same directory")
        logger.info("- Verify Python version is 3.10+")
    
    return passed == total

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
