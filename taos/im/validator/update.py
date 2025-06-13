# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import time
import traceback
import json

# Bittensor
import bittensor as bt

from typing import Tuple

import subprocess
import psutil

from taos.common.utils.misc import run_process
from taos.im.neurons.validator import Validator
        
def check_repo(self : Validator) -> Tuple[bool, bool, bool, bool]:
    try:
        bt.logging.info("Checking repo for updates...")
        remote = self.repo.remotes[self.config.repo.remote]
        fetch = remote.fetch(self.repo.active_branch.name)
        diff = self.repo.head.commit.diff(remote.refs[self.repo.active_branch.name].object.hexsha)
        validator_py_files_changed = False
        simulator_config_changed = False
        simulator_py_files_changed = False
        simulator_cpp_files_changed = False
        for cht in diff.change_type:
            changes = list(diff.iter_change_type(cht))
            for c in changes:
                if str(self.repo_path / c.b_path) == self.simulator_config_file:
                    simulator_config_changed = True
                if c.b_path.endswith('.cpp'):
                    simulator_cpp_files_changed = True
                if c.b_path.endswith('.py'):
                    if 'simulate/trading' in c.b_path:
                        simulator_py_files_changed = True
                    else:
                        validator_py_files_changed = True
        if not any([validator_py_files_changed, simulator_config_changed, simulator_py_files_changed, simulator_cpp_files_changed]):
            bt.logging.info("Nothing to update.")
        else:
            bt.logging.info(f"Changes to pull : [{validator_py_files_changed=}, {simulator_config_changed=}, {simulator_py_files_changed=}, {simulator_cpp_files_changed=}]")
        return validator_py_files_changed, simulator_config_changed, simulator_py_files_changed, simulator_cpp_files_changed
    except Exception as ex:
        self.pagerduty_alert(f"Failed to check repo : {ex}", details={"traceback" : traceback.format_exc()})
        return False, False, False, False

def update_validator(self : Validator) -> None:
    """
    Updates, installs and restarts validator.
    """
    py_cmd = ["pip", "install", "-e", "."]
    bt.logging.info(f"UPDATING VALIDATOR (PY)...")
    make = run_process(py_cmd, cwd=(self.repo_path ).resolve())
    if make.returncode == 0:
        bt.logging.success("VALIDATOR PY UPDATE SUCCESSFUL.  RESTARTING...")
    else:
        raise Exception(f"FAILED TO COMPLETE VALIDATOR PY UPDATE:\n{make.stderr}")

    pm2_json = subprocess.run(['pm2', 'jlist'],capture_output = True, text = True).stdout
    pm2_js = json.loads(pm2_json) if pm2_json else []
    restart_cmd = None
    if len(pm2_js) > 0:
        pm2_processes = {p['name'] : p for p in pm2_js}
        if 'validator' in pm2_processes:
            bt.logging.info("FOUND VALIDATOR IN pm2 PROCESSES.")
            restart_cmd = ["pm2 restart validator"]
    if not restart_cmd:
        for proc in psutil.process_iter():
            if 'python validator.py' in ' '.join(proc.cmdline()):
                bt.logging.info(f"FOUND VALIDATOR PROCESS `{proc.name()}` WITH PID {proc.pid}")
                proc.kill()
        restart_cmd = [f"pm2 start --name=validator \"python validator.py --netuid {self.config.netuid} --subtensor.chain_endpoint {self.config.subtensor.chain_endpoint} --wallet.path {self.config.wallet.path} --wallet.name {self.config.wallet.name} --wallet.hotkey {self.config.wallet.hotkey}\""]
    bt.logging.info(f"RESTARTING VALIDATOR : {' '.join(restart_cmd)}")
    validator = subprocess.run(restart_cmd, cwd=str((self.repo_path / 'taos' / 'im' / 'neurons').resolve()), shell=True, capture_output=True)
    return
    # if validator.returncode == 0:
    #     bt.logging.info("VALIDATOR RESTART SUCCESSFUL.")
    # else:
    #     raise Exception(f"FAILED TO RESTART VALIDATOR:\nSTDOUT : {validator.stdout}\nSTDERR : {validator.stderr}")

def rebuild_simulator(self : Validator) -> None:
    """
    Re-compiles the C++ simulator.
    """
    gcc_version_proc = subprocess.run(['g++ -dumpversion'], shell=True, capture_output=True)
    if gcc_version_proc.returncode == 0:
        gcc_version = gcc_version_proc.stdout
        gcc14_check_proc = subprocess.run(['g++-14 -dumpversion'], shell=True, capture_output=True)
        if gcc14_check_proc.returncode != 0:
            raise Exception(f"Could not find g++-14 on system : {gcc14_check_proc.stderr}")
    else:
        raise Exception(f"Could not find g++ version : {gcc_version_proc.stderr}")
    if gcc_version != '14':
        make_cmd = ["cmake","-DENABLE_TRACES=1", "-DCMAKE_BUILD_TYPE=Release", "..", "-D", "CMAKE_CXX_COMPILER=g++-14"]
    else:
        make_cmd = ["cmake","-DENABLE_TRACES=1", "-DCMAKE_BUILD_TYPE=Release", ".."]

    bt.logging.info(f"REBUILDING SIMULATOR (MAKE)...")
    make = run_process(make_cmd, (self.repo_path / 'simulate' / 'trading' / 'build').resolve())
    if make.returncode == 0:
        bt.logging.success("MAKE PROCESS SUCCESSFUL.  BUILDING...")
        build_cmd = ["cmake", "--build","."]
        bt.logging.info(f"REBUILDING SIMULATOR (BUILD)...")
        build = run_process(build_cmd, cwd=(self.repo_path / 'simulate' / 'trading' / 'build').resolve())
        if build.returncode == 0:
            bt.logging.success("REBUILT SIMULATOR.")
        else:
            raise Exception(f"FAILED TO COMPLETE SIMULATOR BUILD:\n{build.stderr}")
    else:
        raise Exception(f"FAILED TO COMPLETE SIMULATOR MAKE:\n{make.stderr}")

    py_cmd = ["pip", "install", "-e", "."]
    bt.logging.info(f"REBUILDING SIMULATOR (PY)...")
    py = run_process(py_cmd, cwd=(self.repo_path / 'simulate' / 'trading').resolve())
    if py.returncode == 0:
        bt.logging.success("PY UPDATE SUCCESSFUL.")
    else:
        raise Exception(f"FAILED TO COMPLETE SIMULATOR PY UPDATE:\n{make.stderr}")

def restart_simulator(self : Validator) -> None:
    """
    Restarts the C++ simulator process.
    """
    pm2_json = subprocess.run(['pm2', 'jlist'],capture_output = True, text = True).stdout
    pm2_js = json.loads(pm2_json) if pm2_json else []

    restart_cmd = None
    if len(pm2_js) > 0:
        pm2_processes = {p['name'] : p for p in pm2_js}
        if 'simulator' in pm2_processes:
            bt.logging.info("FOUND SIMULATOR IN pm2 PROCESSES.")
            restart_cmd = ["pm2 restart simulator"]
    if not restart_cmd:
        for proc in psutil.process_iter():
            if '../build/src/cpp/taosim' in ' '.join(proc.cmdline()):
                bt.logging.info(f"FOUND SIMULATOR PROCESS `{proc.name()}` WITH PID {proc.pid}")
                proc.kill()
        restart_cmd = [f"pm2 start --no-autorestart --name=simulator \"../build/src/cpp/taosim -f {self.simulator_config_file}\""]
    bt.logging.info(f"RESTARTING SIMULATOR : {' '.join(restart_cmd)}")
    simulator = subprocess.run(restart_cmd, cwd=str((self.repo_path / 'simulate' / 'trading' / 'run').resolve()), shell=True, capture_output=True)
    if simulator.returncode == 0:
        if check_simulator(self):
            bt.logging.success("SIMULATOR RESTART SUCCESSFUL.")
        else:
            self.pagerduty_alert("FAILED TO RESTART SIMULATOR!  NOT FOUND IN PM2 AFTER RESTART.")
    else:
        raise Exception(f"FAILED TO RESTART SIMULATOR:\nSTDOUT : {simulator.stdout}\nSTDERR : {simulator.stderr}")

def check_simulator(self : Validator) -> bool:
    """
    If no new state update has been retrieved for 5 minutes, check if the simulator process is still running.
    If the simulator is found not to be online, restart it.
    TODO: Use checkpointing functionality to resume latest simulation.
    """
    if self.last_state_time and self.last_state_time < time.time() - 300:
        pm2_json = subprocess.run(['pm2', 'jlist'],capture_output = True, text = True).stdout
        pm2_js = json.loads(pm2_json) if pm2_json else []
        pm2_processes = {p['name'] : p for p in pm2_js}
        if 'simulator' in pm2_processes:
            if pm2_processes['simulator']['pm2_env']['status'] != 'online':
                self.pagerduty_alert(f"Simulator process (PM2) has stopped! Status={pm2_processes['simulator']['pm2_env']['status']}")
                return False
            else: 
                return True
        else:
            found = False
            for proc in psutil.process_iter():
                if '../build/src/cpp/taosim' in ' '.join(proc.cmdline()):
                    found = True
            if not found:
                self.pagerduty_alert(f"Simulator process (No PM2) has stopped!")
                return False
            else: 
                return True
    return True