# Advanced Trading Agent for τaos Subnet 79

This is a comprehensive, production-ready trading agent for the τaos subnet that combines multiple trading strategies, machine learning, and advanced risk management.

## 🚀 Features

### Multi-Strategy Approach
- **Momentum Trading**: Captures trending price movements
- **Mean Reversion**: Exploits price deviations from moving averages
- **Arbitrage**: Takes advantage of order book imbalances
- **ML Signals**: Machine learning-based predictions using ensemble methods

### Advanced Risk Management
- Dynamic position sizing based on volatility and confidence
- Portfolio heat management
- Stop-loss and take-profit mechanisms
- Maximum drawdown protection
- Real-time risk monitoring

### Machine Learning Capabilities
- Feature engineering with 15+ technical indicators
- Multiple ML models (Gradient Boosting, Ridge, Lasso, Random Forest)
- Online learning with periodic retraining
- Performance-based model selection

### Performance Monitoring
- Real-time Sharpe ratio calculation
- Win rate and profit factor tracking
- Drawdown monitoring
- Comprehensive logging and metrics

## 📋 Prerequisites

### System Requirements
- **OS**: Ubuntu 20.04+ or similar Linux distribution
- **Python**: 3.10+ (recommended: 3.10.9)
- **RAM**: 4GB+ (8GB+ recommended)
- **CPU**: 4+ cores (8+ cores recommended)
- **Storage**: 10GB+ free space

### Required Software
- Python 3.10+
- pip (Python package manager)
- npm (Node.js package manager)
- PM2 (Process manager)
- tmux (Terminal multiplexer)
- Git

### Python Dependencies
```bash
pip install numpy pandas scikit-learn bittensor joblib
```

## 🛠️ Installation

### 1. Clone the Repository
```bash
git clone https://github.com/taos-im/sn-79
cd sn-79
```

### 2. Install System Dependencies
```bash
# Update system packages
sudo apt update && sudo apt upgrade -y

# Install required packages
sudo apt install -y python3 python3-pip nodejs npm tmux htop curl wget git

# Install PM2 globally
sudo npm install -g pm2
```

### 3. Set Up Python Environment
```bash
# Install Python 3.10.9 using pyenv (recommended)
curl https://pyenv.run | bash
export PATH="$HOME/.pyenv/bin:$PATH"
eval "$(pyenv init -)"
pyenv install 3.10.9
pyenv global 3.10.9

# Install Python dependencies
pip install -e .
```

### 4. Create Bittensor Wallet
```bash
# Create coldkey
btcli wallet new_coldkey --wallet.name taos

# Create hotkey
btcli wallet new_hotkey --wallet.name taos --wallet.hotkey miner

# Register on subnet 79 (mainnet)
btcli wallet register --wallet.name taos --wallet.hotkey miner --netuid 79
```

## 🚀 Deployment

### Quick Start
```bash
# Make deployment script executable
chmod +x deploy_miner.sh

# Deploy with default settings
./deploy_miner.sh

# Deploy with custom settings
./deploy_miner.sh -e wss://entrypoint-finney.opentensor.ai:443 -w taos -h miner -u 79
```

### Manual Deployment
```bash
# Copy agent files
mkdir -p ~/.taos/agents
cp advanced_trading_agent.py ~/.taos/agents/
cp miner_config.yaml ~/.taos/agents/

# Start the miner
cd taos/im/neurons
python3 miner.py \
  --netuid 79 \
  --subtensor.chain_endpoint wss://entrypoint-finney.opentensor.ai:443 \
  --wallet.path ~/.bittensor/wallets/ \
  --wallet.name taos \
  --wallet.hotkey miner \
  --axon.port 8091 \
  --agent.path ~/.taos/agents \
  --agent.name AdvancedTradingAgent \
  --agent.params "max_position_size=10.0 risk_tolerance=0.02 expiry_period=120000000000 momentum_weight=0.3 mean_reversion_weight=0.3 arbitrage_weight=0.2 ml_signal_weight=0.2 ml_model=ensemble feature_window=20 retrain_interval=100" \
  --logging.info
```

## ⚙️ Configuration

### Agent Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `max_position_size` | 10.0 | Maximum position size per trade |
| `risk_tolerance` | 0.02 | Risk tolerance (2% of capital) |
| `expiry_period` | 120000000000 | Order expiry in nanoseconds (2 minutes) |
| `momentum_weight` | 0.3 | Weight for momentum strategy |
| `mean_reversion_weight` | 0.3 | Weight for mean reversion strategy |
| `arbitrage_weight` | 0.2 | Weight for arbitrage strategy |
| `ml_signal_weight` | 0.2 | Weight for ML signals |
| `ml_model` | ensemble | ML model type (ensemble, ridge, lasso, random_forest) |
| `feature_window` | 20 | Historical window for features |
| `retrain_interval` | 100 | Retrain model every N observations |

### Strategy Weights
The agent uses weighted combination of multiple strategies:
- **Momentum**: Captures trending movements
- **Mean Reversion**: Exploits price deviations
- **Arbitrage**: Order book imbalance exploitation
- **ML Signals**: Machine learning predictions

Weights must sum to 1.0 for optimal performance.

## 📊 Monitoring

### View Logs
```bash
# PM2 logs
pm2 logs miner

# Real-time monitoring
tmux attach -t taos_miner

# System monitoring
htop
```

### Performance Metrics
The agent tracks:
- **Sharpe Ratio**: Risk-adjusted returns
- **Win Rate**: Percentage of profitable trades
- **Profit Factor**: Gross profit / Gross loss
- **Maximum Drawdown**: Largest peak-to-trough decline
- **Volatility**: Price movement standard deviation

### Dashboard
Monitor your agent's performance at: https://taos.simulate.trading

## 🔧 Troubleshooting

### Common Issues

#### 1. Import Errors
```bash
# Install missing dependencies
pip install numpy pandas scikit-learn bittensor joblib

# Update bittensor
pip install --upgrade bittensor
```

#### 2. Wallet Issues
```bash
# Check wallet status
btcli wallet overview --wallet.name taos --wallet.hotkey miner

# Re-register if needed
btcli wallet register --wallet.name taos --wallet.hotkey miner --netuid 79
```

#### 3. Performance Issues
```bash
# Check system resources
htop
df -h

# Restart agent
pm2 restart miner

# Check logs for errors
pm2 logs miner --lines 100
```

#### 4. Network Issues
```bash
# Test connectivity
ping entrypoint-finney.opentensor.ai

# Check firewall
sudo ufw status
```

### Debug Mode
```bash
# Enable debug logging
./deploy_miner.sh -l debug

# Check detailed logs
pm2 logs miner --lines 500
```

## 📈 Optimization

### Performance Tuning

#### 1. Hardware Optimization
- **CPU**: More cores = faster feature calculation
- **RAM**: More memory = larger feature windows
- **Network**: Lower latency = better execution

#### 2. Strategy Optimization
- Adjust strategy weights based on market conditions
- Increase ML model complexity for better predictions
- Optimize feature windows for your trading timeframe

#### 3. Risk Management
- Lower risk tolerance for conservative trading
- Increase position sizes for aggressive strategies
- Adjust stop-loss and take-profit levels

### Advanced Configuration

#### Custom Features
Add your own features in `calculate_features()` method:
```python
def calculate_features(self, book: Book, timestamp: int, validator: str) -> Dict[str, float]:
    features = {}
    # Add your custom features here
    features['custom_indicator'] = self.calculate_custom_indicator(book)
    return features
```

#### Custom Strategies
Implement new strategies by extending the agent:
```python
def generate_custom_signal(self, features: Dict[str, float]) -> TradingSignal:
    # Your custom strategy logic
    return TradingSignal(...)
```

## 🎯 Best Practices

### 1. Start Conservative
- Begin with small position sizes
- Use lower risk tolerance initially
- Monitor performance before scaling up

### 2. Monitor Performance
- Check logs regularly
- Monitor Sharpe ratio and win rate
- Adjust parameters based on results

### 3. Risk Management
- Never risk more than you can afford to lose
- Use stop-losses consistently
- Diversify across multiple strategies

### 4. Continuous Improvement
- Analyze performance data
- Experiment with different parameters
- Keep up with subnet updates

## 📚 Additional Resources

### Documentation
- [τaos Whitepaper](https://simulate.trading/taos-im-paper)
- [Bittensor Documentation](https://docs.bittensor.com)
- [Subnet 79 Dashboard](https://taos.simulate.trading)

### Community
- [Bittensor Discord](https://discord.com/channels/799672011265015819/1353733356470276096)
- [τaos GitHub](https://github.com/taos-im/sn-79)
- [Twitter: @taos_im](https://twitter.com/taos_im)

### Support
- Check logs first: `pm2 logs miner`
- Review configuration: `cat ~/.taos/agents/miner_config.yaml`
- Join Discord for community support

## 🏆 Success Tips

1. **Start Small**: Begin with conservative settings
2. **Monitor Closely**: Watch performance metrics daily
3. **Adapt**: Adjust parameters based on market conditions
4. **Learn**: Study successful strategies and patterns
5. **Persist**: Trading success takes time and patience

---

**Good luck with your trading on τaos Subnet 79! 🚀**
