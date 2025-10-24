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
import uvloop
import asyncio
import aiohttp
from typing import List

from taos.im.neurons.validator import Validator
from taos.im.protocol import FinanceAgentResponse, FinanceEventNotification, MarketSimulationStateUpdate
from taos.im.protocol.instructions import *
from taos.im.validator.reward import set_delays
from taos.im.utils.compress import compress, batch_compress
import multiprocessing

asyncio.set_event_loop_policy(uvloop.EventLoopPolicy())

class DendriteManager:
    @staticmethod
    async def configure_session(validator):
        if validator.dendrite._session and not validator.dendrite._session.closed:
            try:
                await validator.dendrite._session.close()
            except Exception:
                pass
        
        connector = aiohttp.TCPConnector(
            ssl=False,
            limit=0,
            limit_per_host=0,
            ttl_dns_cache=300,
            enable_cleanup_closed=True,
            force_close=True,
        )

        timeout = aiohttp.ClientTimeout(
            total=validator.config.neuron.timeout,
            connect=1.0,
            sock_read=1.0,
            sock_connect=1.0,
        )

        validator.dendrite._session = aiohttp.ClientSession(
            connector=connector,
            timeout=timeout,
            skip_auto_headers={'User-Agent'},
        )

def validate_responses(self : Validator, synapses : dict[int, MarketSimulationStateUpdate]) -> None:
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
    success = 0
    timeouts = 0
    failures = 0
    for uid, synapse in synapses.items():
        valid_instructions = []
        if synapse.is_timeout:
            timeouts += 1
            continue
        elif synapse.is_failure:
            failures += 1
            continue
        elif not synapse.is_success:
            failures += 1
            bt.logging.warning(f"UID {uid} invalid state (not success/timeout/failure): {synapse.dendrite.status_message}")
            continue
        success += 1
        if synapse.is_success:
            synapse.decompress()
        if synapse.response:
            if synapse.compressed:
                bt.logging.warning(f"Failed to decompress response for {uid}!")
                synapse.response = None
                continue
            # If agents attempt to submit instructions for agent IDs other than their own, ignore these responses
            if synapse.response.agent_id != uid:
                bt.logging.warning(f"Invalid response submitted by agent {uid} (Mismatched Agent Ids) : {synapse.response}")
                synapse.response = None
                continue
            volume_cap =  round(self.config.scoring.activity.capital_turnover_cap * (self.simulation.miner_wealth), self.simulation.volumeDecimals)
            miner_volumes = {book_id : round(sum(book_volume['total'].values()), self.simulation.volumeDecimals) for book_id, book_volume in self.trade_volumes[uid].items()}        
            for instruction in synapse.response.instructions:
                try:
                    if instruction.agentId != uid or instruction.type == 'RESET_AGENT':
                        bt.logging.warning(f"Invalid instruction submitted by agent {uid} (Mismatched Agent Ids) : {instruction}")
                        valid_instructions = []
                        break
                    if instruction.bookId >= self.simulation.book_count:
                        bt.logging.warning(f"Invalid instruction submitted by agent {uid} (Invalid Book Id {instruction.bookId}) : {instruction}")
                        continue
                    # If a miner exceeds `capital_turnover_cap` times their initial wealth in trading volume over a single `trade_volume_assessment_period`, they are restricted from placing additional orders.
                    # Only cancellations may be submitted and processed by the miner until their volume on the specified book in the previous period is below the cap.
                    if miner_volumes[instruction.bookId] >= volume_cap and instruction.type != "CANCEL_ORDERS":
                        bt.logging.debug(f"Agent {uid} has reached their volume cap on book {instruction.bookId} : Traded {miner_volumes[instruction.bookId]} / {volume_cap}.")
                        continue
                    if instruction.type in ['PLACE_ORDER_MARKET', 'PLACE_ORDER_LIMIT'] and instruction.stp == STP.NO_STP:
                        instruction.stp = STP.CANCEL_OLDEST
                    valid_instructions.append(instruction)
                except Exception as ex:
                    bt.logging.warning(f"Error processing instruction submitted by agent {uid} : {ex}\n{instruction}")
            final_instructions = []
            # Enforce the configured limit on maximum submitted instructions (to prevent overloading simulator)
            instructions_per_book = {}
            for instruction in valid_instructions:
                if hasattr(instruction, 'bookId') and instruction.bookId not in instructions_per_book:
                    instructions_per_book[instruction.bookId] = 0
                instructions_per_book[instruction.bookId] += 1
                if instructions_per_book[instruction.bookId] <= self.config.scoring.max_instructions_per_book:
                    final_instructions.append(instruction)
            if len(final_instructions) < len(valid_instructions):
                bt.logging.warning(f"Agent {uid} sent {len(valid_instructions)} instructions (Avg. {len(valid_instructions) / len(instructions_per_book)} / book), with more than {self.config.scoring.max_instructions_per_book} instructions on some books - excess instructions were dropped.  Final instruction count {len(final_instructions)}.")
                for book_id, count in instructions_per_book.items():
                    bt.logging.debug(f"Agent {uid} Book {book_id} : {count} Instructions")
            # Update the synapse response with only the validated instructions
            synapse.response.instructions = final_instructions
            total_responses += 1
            total_instructions += len(synapse.response.instructions)
        else:
            bt.logging.debug(f"UID {uid} failed to respond : {synapse.dendrite.status_message}")    
    return total_responses, total_instructions, success, timeouts, failures

def update_stats(self : Validator, synapses : dict[int, MarketSimulationStateUpdate]) -> None:
    """
    Updates miner request statistics maintained and published by validator

    Args:
        self (taos.im.neurons.validator.Validator): The intelligent markets simulation validator.
        synapses (list[taos.im.protocol.MarketSimulationStateUpdate]): The synapses with attached agent responses to be evaluated for statistics update.
    Returns:
        None
    """
    for uid, synapse in synapses.items():
        self.miner_stats[uid]['requests'] += 1
        if synapse.is_timeout:
            self.miner_stats[uid]['timeouts'] += 1
        elif synapse.is_failure or synapse.response is None:
            self.miner_stats[uid]['failures'] += 1
        elif synapse.is_blacklist:
            self.miner_stats[uid]['rejections'] += 1
        elif synapse.dendrite.process_time:            
            self.miner_stats[uid]['call_time'].append(synapse.dendrite.process_time)

async def forward(self: Validator, synapse: MarketSimulationStateUpdate) -> List[FinanceAgentResponse]:
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
        response = FinanceAgentResponse(agent_id=self.uid)
        response.reset_agents(agent_ids=self.deregistered_uids)
        responses.append(response)

    bt.logging.info(f"Querying Miners...")
    start = time.time()
        
    await DendriteManager.configure_session(self)

    synapse_start = time.time()
    start = time.time()
    # Create miner query synapses
    compressed_books = compress(
        synapse.books,
        level=self.config.compression.level,
        engine=self.config.compression.engine,
        version=synapse.version,
    )
    bt.logging.info(f"Compressed books ({time.time()-start:.4f}s).")
    
    serialized_config = synapse.config.model_dump(mode='json')
    def create_axon_synapse(uid):
        return synapse.model_copy(update={
            "accounts": {uid: synapse.accounts[uid]},
            "notices": {uid: synapse.notices[uid]},
            "config" : serialized_config
        })

    start = time.time()
    axon_synapses = {uid: create_axon_synapse(uid) for uid in range(len(self.metagraph.axons))}
    bt.logging.info(f"Created axon synapses ({time.time()-start:.4f}s)")
    # Compress miner synapse data
    if self.config.compression.parallel_workers == 0:
        def compress_axon_synapse(synapse):
            return synapse.compress(
                level=self.config.compression.level,
                engine=self.config.compression.engine,
                compressed_books=compressed_books
            )

        axon_synapses = {uid: compress_axon_synapse(axon_synapses[uid]) for uid in range(len(self.metagraph.axons))}
    else:
        num_processes = self.config.compression.parallel_workers if self.config.compression.parallel_workers > 0 else multiprocessing.cpu_count() // 2
        num_axons = len(self.metagraph.axons)
        batches = [self.metagraph.uids[i:i+int(num_axons/num_processes)] for i in range(0, num_axons, int(num_axons/num_processes))]
        axon_synapses = batch_compress(
            axon_synapses,
            compressed_books,
            batches,
            level=self.config.compression.level,
            engine=self.config.compression.engine,
            version=synapse.version
        )        
    bt.logging.info(f"Compressed synapses ({time.time()-synapse_start:.4f}s).")

    start = time.time()
    synapse_responses = {}

    async def query_uid(uid):
        try:
            response = await asyncio.wait_for(
                self.dendrite(
                    axons=self.metagraph.axons[uid],
                    synapse=axon_synapses[uid],
                    timeout=self.config.neuron.timeout,
                    deserialize=False
                ),
                timeout=self.config.neuron.query_timeout
            )
            return uid, response
        except asyncio.CancelledError:
            # Task was cancelled due to global timeout
            bt.logging.debug(f"Query cancelled for UID {uid}")
            axon_synapses[uid] = self.dendrite.preprocess_synapse_for_request(
                self.metagraph.axons[uid],
                axon_synapses[uid],
                self.config.neuron.timeout
            )
            axon_synapses[uid].dendrite.status_code = 408
            return uid, axon_synapses[uid]
        except asyncio.TimeoutError:
            bt.logging.debug(f"Timeout querying UID {uid}")
            axon_synapses[uid] = self.dendrite.preprocess_synapse_for_request(
                self.metagraph.axons[uid],
                axon_synapses[uid],
                self.config.neuron.timeout
            )
            axon_synapses[uid].dendrite.status_code = 408
            return uid, axon_synapses[uid]
        except Exception as e:
            bt.logging.debug(f"Error querying UID {uid}: {e}")
            axon_synapses[uid] = self.dendrite.preprocess_synapse_for_request(
                self.metagraph.axons[uid],
                axon_synapses[uid],
                self.config.neuron.timeout
            )
            axon_synapses[uid].dendrite.status_code = 500
            return uid, axon_synapses[uid]
    # Create query tasks
    query_tasks = []
    for uid in range(len(self.metagraph.axons)):
        if uid not in self.deregistered_uids:
            query_tasks.append(asyncio.create_task(query_uid(uid)))
    
    bt.logging.debug(f"Created {len(query_tasks)} query tasks")

    async def collect_results():
        done, pending = await asyncio.wait(
            query_tasks,
            timeout=self.config.neuron.global_query_timeout,
            return_when=asyncio.ALL_COMPLETED
        )
        
        results = {}
        for task in done:
            try:
                uid, response = task.result()
                results[uid] = response
            except Exception as e:
                bt.logging.debug(f"Error getting task result: {e}")
        
        if pending:
            bt.logging.warning(f"Global timeout hit, cancelling {len(pending)} pending tasks")
            for task in pending:
                if not task.done():
                    task.cancel()
        
        return results
    # Query miners
    try:
        synapse_responses = await asyncio.wait_for(
            collect_results(),
            timeout=self.config.neuron.global_query_timeout + 0.1
        )
    except asyncio.TimeoutError:
        bt.logging.error(f"Hard timeout enforced after {self.config.neuron.global_query_timeout}s")
        synapse_responses = {}
        for task in query_tasks:
            if task.done() and not task.cancelled():
                try:
                    uid, response = task.result()
                    synapse_responses[uid] = response
                except Exception:
                    pass
        for task in query_tasks:
            if not task.done():
                task.cancel()
    except Exception as e:
        bt.logging.error(f"Unexpected error during query process: {e}")
        synapse_responses = {}
        for task in query_tasks:
            if task.done() and not task.cancelled():
                try:
                    uid, response = task.result()
                    synapse_responses[uid] = response
                except Exception:
                    pass

    bt.logging.info(f"Dendrite call completed ({time.time()-start:.4f}s | "
                    f"Timeout {self.config.neuron.timeout}s / {self.config.neuron.query_timeout}s / {self.config.neuron.global_query_timeout}s).")
    self.dendrite.synapse_history = self.dendrite.synapse_history[-10:]
    
    # Validate the miner responses
    start = time.time()
    total_responses, total_instructions, success, timeouts, failures = validate_responses(self, synapse_responses)
    bt.logging.info(f"Validated Responses ({time.time()-start:.4f}s).")
    
    # Update miner statistics
    start = time.time()
    update_stats(self, synapse_responses)
    bt.logging.debug(f"Updated Stats ({time.time()-start:.4f}s).")
    
    # Set simulation delays and add modified responses
    start = time.time()
    responses.extend(set_delays(self, synapse_responses))
    bt.logging.debug(f"Set Delays ({time.time()-start:.4f}s).")
    bt.logging.trace(f"Responses: {responses}")    
    bt.logging.info(f"Received {total_responses} valid responses containing {total_instructions} instructions "
                    f"({success} SUCCESS | {timeouts} TIMEOUTS | {failures} FAILURES).")
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
        axons = [self.metagraph.axons[notice.event.agentId]] if notice.event.agentId else self.metagraph.axons
        responses.extend(await self.dendrite(
            axons=axons,
            synapse=notice,
            timeout=1
        ))
    for response in responses:
        if response and response.acknowledged:
            bt.logging.info(f"{response[0].type} EventNotification Acknowledged by {response[0].axon.hotkey}")
