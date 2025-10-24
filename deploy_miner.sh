#!/bin/bash

# Advanced Trading Agent Deployment Script for τaos Subnet 79
# This script sets up and deploys the advanced trading agent

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default configuration
ENDPOINT="wss://entrypoint-finney.opentensor.ai:443"
WALLET_PATH="$HOME/.bittensor/wallets/"
WALLET_NAME="taos"
HOTKEY_NAME="miner"
NETUID=79
AXON_PORT=8091
AGENT_PATH="$HOME/.taos/agents"
AGENT_NAME="AdvancedTradingAgent"
LOG_LEVEL="info"
CONFIG_FILE="miner_config.yaml"

# Parse command line arguments
while getopts e:p:w:h:u:a:g:n:l:c: flag
do
    case "${flag}" in
        e) ENDPOINT=${OPTARG};;
        p) WALLET_PATH=${OPTARG};;
        w) WALLET_NAME=${OPTARG};;
        h) HOTKEY_NAME=${OPTARG};;
        u) NETUID=${OPTARG};;
        a) AXON_PORT=${OPTARG};;
        g) AGENT_PATH=${OPTARG};;
        n) AGENT_NAME=${OPTARG};;
        l) LOG_LEVEL=${OPTARG};;
        c) CONFIG_FILE=${OPTARG};;
    esac
done

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}τaos Advanced Trading Agent Deployment${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Display configuration
echo -e "${YELLOW}Configuration:${NC}"
echo "Endpoint: $ENDPOINT"
echo "Wallet Path: $WALLET_PATH"
echo "Wallet Name: $WALLET_NAME"
echo "Hotkey Name: $HOTKEY_NAME"
echo "Netuid: $NETUID"
echo "Axon Port: $AXON_PORT"
echo "Agent Path: $AGENT_PATH"
echo "Agent Name: $AGENT_NAME"
echo "Log Level: $LOG_LEVEL"
echo "Config File: $CONFIG_FILE"
echo ""

# Check if running as root
if [[ $EUID -eq 0 ]]; then
   echo -e "${YELLOW}Warning: Running as root. Consider using a non-root user for security.${NC}"
fi

# Check dependencies
echo -e "${BLUE}Checking dependencies...${NC}"

# Check if Python 3.10+ is available
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}Error: Python 3 is not installed${NC}"
    exit 1
fi

PYTHON_VERSION=$(python3 -c 'import sys; print(".".join(map(str, sys.version_info[:2])))')
if [[ $(echo "$PYTHON_VERSION < 3.10" | bc -l) -eq 1 ]]; then
    echo -e "${RED}Error: Python 3.10+ is required. Current version: $PYTHON_VERSION${NC}"
    exit 1
fi

echo -e "${GREEN}Python version: $PYTHON_VERSION${NC}"

# Check if required packages are installed
echo -e "${BLUE}Checking Python packages...${NC}"

REQUIRED_PACKAGES=("numpy" "pandas" "scikit-learn" "bittensor")
MISSING_PACKAGES=()

for package in "${REQUIRED_PACKAGES[@]}"; do
    if ! python3 -c "import $package" 2>/dev/null; then
        MISSING_PACKAGES+=("$package")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
    echo -e "${YELLOW}Missing packages: ${MISSING_PACKAGES[*]}${NC}"
    echo -e "${BLUE}Installing missing packages...${NC}"
    pip3 install "${MISSING_PACKAGES[@]}"
fi

# Check if pm2 is installed
if ! command -v pm2 &> /dev/null; then
    echo -e "${YELLOW}PM2 not found. Installing PM2...${NC}"
    npm install -g pm2
fi

# Check if tmux is installed
if ! command -v tmux &> /dev/null; then
    echo -e "${YELLOW}tmux not found. Installing tmux...${NC}"
    if command -v apt-get &> /dev/null; then
        sudo apt-get update && sudo apt-get install -y tmux
    elif command -v yum &> /dev/null; then
        sudo yum install -y tmux
    else
        echo -e "${RED}Please install tmux manually${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}All dependencies satisfied${NC}"

# Create necessary directories
echo -e "${BLUE}Creating directories...${NC}"
mkdir -p "$AGENT_PATH"
mkdir -p "$HOME/.taos/performance_data"
mkdir -p "$HOME/.taos/logs"

# Copy agent files
echo -e "${BLUE}Setting up agent files...${NC}"
if [ -f "advanced_trading_agent.py" ]; then
    cp advanced_trading_agent.py "$AGENT_PATH/"
    echo -e "${GREEN}Advanced trading agent copied${NC}"
else
    echo -e "${RED}Error: advanced_trading_agent.py not found${NC}"
    exit 1
fi

if [ -f "$CONFIG_FILE" ]; then
    cp "$CONFIG_FILE" "$AGENT_PATH/"
    echo -e "${GREEN}Configuration file copied${NC}"
else
    echo -e "${YELLOW}Warning: Configuration file $CONFIG_FILE not found${NC}"
fi

# Check if wallet exists
echo -e "${BLUE}Checking wallet...${NC}"
if [ ! -d "$WALLET_PATH/$WALLET_NAME" ]; then
    echo -e "${RED}Error: Wallet $WALLET_NAME not found at $WALLET_PATH${NC}"
    echo -e "${YELLOW}Please create a wallet first using:${NC}"
    echo "btcli wallet new_coldkey --wallet.name $WALLET_NAME"
    echo "btcli wallet new_hotkey --wallet.name $WALLET_NAME --wallet.hotkey $HOTKEY_NAME"
    exit 1
fi

# Check if hotkey is registered
echo -e "${BLUE}Checking registration...${NC}"
if ! btcli wallet overview --wallet.name "$WALLET_NAME" --wallet.hotkey "$HOTKEY_NAME" --netuid "$NETUID" --subtensor.network finney 2>/dev/null | grep -q "Registered"; then
    echo -e "${YELLOW}Warning: Hotkey may not be registered on netuid $NETUID${NC}"
    echo -e "${YELLOW}Please register your hotkey first using:${NC}"
    echo "btcli wallet register --wallet.name $WALLET_NAME --wallet.hotkey $HOTKEY_NAME --netuid $NETUID"
fi

# Create agent parameters from config file
AGENT_PARAMS=""
if [ -f "$AGENT_PATH/$CONFIG_FILE" ]; then
    # Extract parameters from YAML config (simplified)
    AGENT_PARAMS="max_position_size=10.0 risk_tolerance=0.02 expiry_period=120000000000 momentum_weight=0.3 mean_reversion_weight=0.3 arbitrage_weight=0.2 ml_signal_weight=0.2 ml_model=ensemble feature_window=20 retrain_interval=100"
else
    # Default parameters
    AGENT_PARAMS="max_position_size=10.0 risk_tolerance=0.02 expiry_period=120000000000 momentum_weight=0.3 mean_reversion_weight=0.3 arbitrage_weight=0.2 ml_signal_weight=0.2 ml_model=ensemble feature_window=20 retrain_interval=100"
fi

# Stop existing miner if running
echo -e "${BLUE}Stopping existing miner...${NC}"
pm2 delete miner 2>/dev/null || true

# Start the miner
echo -e "${BLUE}Starting advanced trading agent...${NC}"

# Create the pm2 start command
PM2_CMD="python3 $AGENT_PATH/$AGENT_NAME.py --netuid $NETUID --subtensor.chain_endpoint $ENDPOINT --wallet.path $WALLET_PATH --wallet.name $WALLET_NAME --wallet.hotkey $HOTKEY_NAME --axon.port $AXON_PORT --agent.path $AGENT_PATH --agent.name $AGENT_NAME --agent.params \"$AGENT_PARAMS\" --logging.$LOG_LEVEL"

# Start with pm2
pm2 start --name=miner "$PM2_CMD"
pm2 save

echo -e "${GREEN}Advanced trading agent started successfully!${NC}"
echo ""

# Display status
echo -e "${BLUE}Agent Status:${NC}"
pm2 status

echo ""
echo -e "${BLUE}Useful Commands:${NC}"
echo "View logs: pm2 logs miner"
echo "Restart: pm2 restart miner"
echo "Stop: pm2 stop miner"
echo "Status: pm2 status"
echo ""

# Start tmux session for monitoring
echo -e "${BLUE}Starting monitoring session...${NC}"
tmux new-session -d -s taos_miner "pm2 logs miner"
echo -e "${GREEN}Monitoring session started. Attach with: tmux attach -t taos_miner${NC}"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Deployment Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}Next Steps:${NC}"
echo "1. Monitor logs: tmux attach -t taos_miner"
echo "2. Check performance: pm2 logs miner"
echo "3. Monitor your agent's performance on the dashboard"
echo "4. Adjust parameters in $AGENT_PATH/$CONFIG_FILE if needed"
echo ""
echo -e "${BLUE}Good luck with your trading!${NC}"
