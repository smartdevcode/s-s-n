#!/bin/sh
ENDPOINT=wss://entrypoint-finney.opentensor.ai:443
WALLET_PATH=~/.bittensor/wallets/
WALLET_NAME=taos
HOTKEY_NAME=validator
NETUID=1
CHECKPOINT=0
LOG_LEVEL=info
while getopts e:p:w:h:u:l: flag
do
    case "${flag}" in
        e) ENDPOINT=${OPTARG};;
        p) WALLET_PATH=${OPTARG};;
        w) WALLET_NAME=${OPTARG};;
        h) HOTKEY_NAME=${OPTARG};;
        u) NETUID=${OPTARG};;
        l) LOG_LEVEL=${OPTARG};;
        # c) CHECKPOINT=${OPTARG};;
    esac
done
echo "ENDPOINT: $ENDPOINT"
echo "WALLET_PATH: $WALLET_PATH"
echo "WALLET_NAME: $WALLET_NAME"
echo "HOTKEY_NAME: $HOTKEY_NAME"
echo "NETUID: $NETUID"
echo "CHECKPOINT: $CHECKPOINT"

pm2 delete simulator validator
tmux kill-session -t taos

git pull
pip install -e .
echo "Starting Validator"
cd taos/im/neurons
pm2 start --name=validator "python validator.py --netuid $NETUID --subtensor.chain_endpoint $ENDPOINT --wallet.path $WALLET_PATH --wallet.name $WALLET_NAME --wallet.hotkey $HOTKEY_NAME --logging.$LOG_LEVEL"

echo "Starting Simulator"
export LD_LIBRARY_PATH="/usr/local/gcc-14.1.0/lib/../lib64:$LD_LIBRARY_PATH"
cd ../../../simulate/trading
git pull
if ! g++ -dumpversion | grep -q "14"; then
	cd build && cmake -DENABLE_TRACES=1 -DCMAKE_BUILD_TYPE=Release -D CMAKE_CXX_COMPILER=g++-14 .. && cmake --build . -j "$(nproc)"
else
	cd build && cmake -DENABLE_TRACES=1 -DCMAKE_BUILD_TYPE=Release -D .. && cmake --build . -j "$(nproc)"
fi
cd ../run
# if [ $CHECKPOINT = 0 ]; then
	pm2 start --no-autorestart --name=simulator "../build/src/cpp/taosim -f config/simulation_0.xml"
# else
	# pm2 start --no-autorestart --name=simulator "../build/src/cpp/taosim -c $CHECKPOINT"
# fi
pm2 save
pm2 startup

echo "Setting Up Tmux Session"
# Start a new tmux session and open htop in the first pane
tmux new-session -d -s taos -n 'validator' 'htop'    
# Start a new tmux session and open the validator logs in the first pane
tmux split-window -v -t taos:validator 'pm2 logs validator'    
# Split the window and open the simulator logs in the new pane
tmux split-window -h -t taos:validator 'pm2 logs simulator'
# Attach to the new tmux session
tmux attach-session -t taos