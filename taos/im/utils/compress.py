# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
from loky.backend.context import set_start_method
set_start_method('forkserver', force=True)
from loky import get_reusable_executor

import zlib
import lz4.frame
import pybase64
import msgspec
from typing import Literal

def compress(payload, level=1, engine : Literal["zlib", "lz4"] = "lz4"):
        """
        Compress a JSON encoded payload.
        """
        try:
            match engine:
                case "zlib":
                    compressor = zlib.compress
                case "lz4":
                    compressor = lz4.frame.compress
                case _:
                    raise Exception(f"Invalid compression engine `{engine}` - allowed values are `zlib` or `lz4`")
            return pybase64.b64encode(compressor(msgspec.json.encode(payload),level)).decode("ascii")
        except Exception as ex:
            print(f"Failed to compress! {ex}")
            return None
    
def compress_batch(axon_synapses):
    return {uid : compress(axon_synapse, level=1, engine='lz4') for uid, axon_synapse in axon_synapses.items()}

def batch_compress(payloads, batches):
    axon_synapse_batches= []
    pool = get_reusable_executor(max_workers=len(batches))
    tasks = [pool.submit(compress_batch, {uid : payloads[uid] for uid in batch}) for batch in batches]
    for task in tasks:
        result = task.result()
        axon_synapse_batches.append(result)
    return {k: v for d in axon_synapse_batches for k, v in d.items()}