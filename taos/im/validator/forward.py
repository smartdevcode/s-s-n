# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
# The MIT License (MIT)
# Copyright © 2023 Yuma Rao
# Copyright © 2025 Rayleigh Research

# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the “Software”), to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all copies or substantial portions of
# the Software.

# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
# THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

import time
import bittensor as bt
from typing import List

from taos.im.neurons.validator import Validator
from taos.im.protocol import FinanceAgentResponse, FinanceEventNotification, MarketSimulationStateUpdate
from taos.im.protocol.instructions import *
from taos.im.validator.reward import set_delays

def validate_responses(self : Validator, synapses : List[MarketSimulationStateUpdate]) -> None:
    """
    Checks responses from miners for any attempts at invalid actions, and enforces limits on instruction counts

    Args:
        self (taos.im.neurons.validator.Validator): The intelligent markets simulation validator.
        synapses (list[taos.im.protocol.MarketSimulationStateUpdate]): The synapses with attached agent responses to be validated.
    Returns:
        None
    """
    total_responses = 0
    total_instructions = 0
    for uid, synapse in enumerate(synapses):
        valid_instructions = []
        if synapse.response:
            synapse.decompress()
            if synapse.compressed:
                bt.logging.warning(f"Failed to decompress response for {uid}!")
                synapse.response = None
                continue
            # If agents attempt to submit instructions for agent IDs other than their own, ignore these responses
            if synapse.response.agent_id != uid:
                bt.logging.warning(f"Invalid response submitted by agent {uid} (Mismatched Agent Ids) : {synapse.response}")
                synapse.response = None
                continue
            for instruction in synapse.response.instructions:
                if instruction.agentId != uid or instruction.type == 'RESET_AGENT':
                    bt.logging.warning(f"Invalid instruction submitted by agent {uid} (Mismatched Agent Ids) : {instruction}")
                    valid_instructions = []
                    break
                valid_instructions.append(instruction)
            final_instructions = []
            # Enforce the configured limit on maximum submitted instructions (to prevent overloading simulator)
            instructions_per_book = {}
            for instruction in valid_instructions:
                if hasattr(instruction, 'bookId') and instruction.bookId not in instructions_per_book:
                    instructions_per_book[instruction.bookId] = 0
                instructions_per_book[instruction.bookId] += 1
                if instructions_per_book[instruction.bookId] < self.config.scoring.max_instructions_per_book:
                    final_instructions.append(instruction)
            if len(final_instructions) < len(valid_instructions):
                bt.logging.warning(f"Agent {uid} sent more than {self.config.scoring.max_instructions_per_book} instructions on some books - excess instructions were dropped.")
            # Update the synapse response with only the validated instructions
            synapse.response.instructions = final_instructions
            total_responses += 1
            total_instructions += len(synapse.response.instructions)
        else:
            bt.logging.debug(f"UID {uid} failed to respond : {synapse.dendrite.status_message}")    
    bt.logging.info(f"Received {total_responses} valid responses containing {total_instructions} instructions.")

def update_stats(self : Validator, synapses : List[MarketSimulationStateUpdate]) -> None:
    """
    Updates miner request statistics maintained and published by validator

    Args:
        self (taos.im.neurons.validator.Validator): The intelligent markets simulation validator.
        synapses (list[taos.im.protocol.MarketSimulationStateUpdate]): The synapses with attached agent responses to be evaluated for statistics update.
    Returns:
        None
    """
    for uid, synapse in enumerate(synapses):
        self.miner_stats[uid]['requests'] += 1
        if synapse.is_timeout:
            self.miner_stats[uid]['timeouts'] += 1
        elif synapse.is_failure or synapse.response is None:
            self.miner_stats[uid]['failures'] += 1
        elif synapse.is_blacklist:
            self.miner_stats[uid]['rejections'] += 1
        elif synapse.dendrite.process_time:            
            self.miner_stats[uid]['call_time'].append(synapse.dendrite.process_time)

async def forward(self : Validator, synapse : MarketSimulationStateUpdate) -> List[FinanceAgentResponse]:
    """
    Forwards state update to miners, validates responses, calculates rewards and handles deregistered UIDs.

    Args:
        self (taos.im.neurons.validator.Validator): The intelligent markets simulation validator.
        synapse : The market state update synapse to be forwarded to miners
    Returns:
        List[FinanceAgentResponse] : Successfully validated responses generated by queried agents.
    """ 
    responses = []
    if self.deregistered_uids != []:
        # Account balances must be reset in the simulator for deregistered agent IDs.
        # The validator constructs a response containing the agent reset instruction for this purpose.
        response = FinanceAgentResponse(agent_id=self.uid)
        response.reset_agents(agent_ids=self.deregistered_uids)
        responses.append(response)

    # Forward the simulation state update to all miners in the network
    bt.logging.info(f"Querying Miners...")
    start = time.time()
    synapse_responses = await self.dendrite(
        axons=self.metagraph.axons,
        synapse=synapse.compress(),
        timeout=self.config.neuron.timeout,
        deserialize=False
    )
    bt.logging.debug(f"Dendrite call completed ({time.time()-start:.4f}s).")
    self.dendrite.synapse_history = self.dendrite.synapse_history[-10:]
    
    # Validate the miner responses
    bt.logging.info(f"Validating Responses...")
    validate_responses(self, synapse_responses)
    
    # Update miner statistics
    bt.logging.debug(f"Updating Stats...")
    start = time.time()
    update_stats(self, synapse_responses)
    
    # Set the simulation time delays on the instructions proportional to the response time,
    # and add the modified responses to the return array.
    bt.logging.debug(f"Setting Delays...")
    responses.extend(set_delays(self, synapse_responses))
    bt.logging.debug(f"Responses: {responses}")   
    return responses

async def notify(self : Validator, notices : List[FinanceEventNotification]) -> None:
    """
    Forwards event notifications to the related miner agents.

    Args:
        self (taos.im.neurons.validator.Validator): The intelligent markets simulation validator.
        notices (List[FinanceEventNotification]) : The notice events published by the validator.
    Returns:
        None
    """
    responses = []
    for notice in notices:
        axons = self.metagraph.axons[notice.event.agentId] if notice.event.agentId else self.metagraph.axons
        responses.append(await self.dendrite(
            axons=axons,
            synapse=notice,
        ))
    for response in responses:
        if response and response.acknowledged:
            bt.logging.info(f"{response.type} EventNotification Acknowledged by {response.axon.hotkey}")
