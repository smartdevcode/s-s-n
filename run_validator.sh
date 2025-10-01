#!/bin/sh
ENDPOINT=wss://entrypoint-finney.opentensor.ai:443
WALLET_PATH=~/.bittensor/wallets/
WALLET_NAME=taos
HOTKEY_NAME=validator
NETUID=79
CHECKPOINT=0
LOG_LEVEL=info
PD_KEY="\"\""
PROM_PORT=9001
TIMEOUT=3.0
SIMULATION_CONFIG=simulation_0
PRESERVE_SIMULATOR=0
while getopts e:p:w:h:u:l:d:o:t:c:s: flag
do
    case "${flag}" in
        e) ENDPOINT=${OPTARG};;
        p) WALLET_PATH=${OPTARG};;
        w) WALLET_NAME=${OPTARG};;
        h) HOTKEY_NAME=${OPTARG};;
        u) NETUID=${OPTARG};;
        l) LOG_LEVEL=${OPTARG};;
        d) PD_KEY=${OPTARG};;
        o) PROM_PORT=${OPTARG};;
        t) TIMEOUT=${OPTARG};;
        c) SIMULATION_CONFIG=${OPTARG};;
        s) PRESERVE_SIMULATOR=${OPTARG};;
        # c) CHECKPOINT=${OPTARG};;
    esac
done
echo "ENDPOINT: $ENDPOINT"
echo "WALLET_PATH: $WALLET_PATH"
echo "WALLET_NAME: $WALLET_NAME"
echo "HOTKEY_NAME: $HOTKEY_NAME"
echo "NETUID: $NETUID"
echo "CHECKPOINT: $CHECKPOINT"
echo "PAGERDUTY KEY: $PD_KEY"
echo "PROMETHEUS PORT: $PROM_PORT"
echo "TIMEOUT: $TIMEOUT"
echo "SIMULATION_CONFIG: $SIMULATION_CONFIG"
echo "PRESERVE_SIMULATOR: $PRESERVE_SIMULATOR"

if [ $PRESERVE_SIMULATOR = 0 ]; then
    pm2 delete simulator validator
    tmux kill-session -t taos
else
    pm2 delete validator
fi

echo "Updating Validator"
git pull
pip install -e .

if [ $PRESERVE_SIMULATOR = 0 ]; then
    echo "Updating Simulator"
    export LD_LIBRARY_PATH="/usr/local/gcc-14.1.0/lib/../lib64:$LD_LIBRARY_PATH"
    cd simulate/trading
    cd vcpkg
    CURRENT_VCPKG_HASH=$(git rev-parse HEAD)
    EXPECTED_VCPKG_HASH="e140b1fde236eb682b0d47f905e65008a191800f"
    echo "Current vcpkg commit:  $CURRENT_VCPKG_HASH"
    echo "Expected vcpkg commit: $EXPECTED_VCPKG_HASH"
    if [ "$CURRENT_VCPKG_HASH" != "$EXPECTED_VCPKG_HASH" ]; then
        echo "Updating vcpkg..."
        git fetch --all --quiet
        git reset --hard "$EXPECTED_VCPKG_HASH"
        echo "Repo successfully reset to $EXPECTED_VCPKG_HASH."
        rm -r ../build
        mkdir ../build
        ./bootstrap-vcpkg.sh -disableMetrics
    else
        echo "Commit hash matches. No reset needed."
    fi
    cd ..
    if ! g++ -dumpversion | grep -q "14"; then
        cd build && cmake -DENABLE_TRACES=1 -DCMAKE_BUILD_TYPE=Release -D CMAKE_CXX_COMPILER=g++-14 .. && cmake --build . -j "$(nproc)"
    else
        cd build && cmake -DENABLE_TRACES=1 -DCMAKE_BUILD_TYPE=Release -D .. && cmake --build . -j "$(nproc)"
    fi
    cd ../../../taos/im/neurons
else
    cd taos/im/neurons
fi

echo "Starting Validator"
pm2 start --name=validator "python validator.py --netuid $NETUID --subtensor.chain_endpoint $ENDPOINT --wallet.path $WALLET_PATH --wallet.name $WALLET_NAME --wallet.hotkey $HOTKEY_NAME --logging.$LOG_LEVEL --alerting.pagerduty.integration_key $PD_KEY --prometheus.port $PROM_PORT --neuron.timeout $TIMEOUT --simulation.xml_config ../../../simulate/trading/run/config/$SIMULATION_CONFIG.xml"

if [ $PRESERVE_SIMULATOR = 0 ]; then
    echo "Starting Simulator"
    cd ../../../simulate/trading/run
    # if [ $CHECKPOINT = 0 ]; then
        pm2 start --no-autorestart --name=simulator "../build/src/cpp/taosim -f config/$SIMULATION_CONFIG.xml"
    # else
        # pm2 start --no-autorestart --name=simulator "../build/src/cpp/taosim -c $CHECKPOINT"
    # fi
    pm2 save
    pm2 startup

    echo "Setting Up Tmux Session"
    # Start a new tmux session and open htop for validator process monitoring in the first pane
    tmux new-session -d -s taos -n 'validator' 'htop -F validator.py'    
    # Split the window horizontally and open htop for simulator resource usage monitoring
    tmux split-window -h -t taos:validator 'htop -F taosim'
    # Focus the first pane
    tmux select-pane -t 0
    # Split vertically and open the validator logs in the third pane
    tmux split-window -v -t taos:validator 'pm2 logs validator'
    # Focus the second pane
    tmux select-pane -t 2
    # Split the window and open the simulator logs in the new pane
    tmux split-window -v -t taos:validator 'pm2 logs simulator'
fi
# Attach to the new tmux session
tmux attach-session -t taos