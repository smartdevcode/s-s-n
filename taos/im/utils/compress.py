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

def decompress(payload, engine : Literal["zlib", "lz4"] = "lz4"):
        """
        Compress a JSON encoded payload.
        """
        try:
            match engine:
                case "zlib":
                    decompressor = zlib.decompress
                case "lz4":
                    decompressor = lz4.frame.decompress
            if isinstance(payload, str):
                decompressed = msgspec.json.decode(decompressor(pybase64.b64decode(payload)))
            else:
                decompressed_payload = msgspec.json.decode(decompressor(pybase64.b64decode(payload['payload'])))             
                decompressed = ({"books" : msgspec.json.decode(decompressor(pybase64.b64decode(payload['books']))) if payload['books'] else {}}) | decompressed_payload
            return decompressed
        except Exception as ex:
            print(f"Failed to compress! {ex}")
            return None

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
    
def compress_batch(payloads, level : int = 1, engine : str = 'lz4'):
    return {uid : compress(payload, level=level, engine=engine) for uid, payload in payloads.items()}

def batch_compress(payloads, batches : list[list[int]], level : int = 1, engine : str = 'lz4'):
    payload_batches= []
    pool = get_reusable_executor(max_workers=len(batches))
    tasks = [pool.submit(compress_batch, {uid : payloads[uid] for uid in batch}, level, engine) for batch in batches]
    for task in tasks:
        result = task.result()
        payload_batches.append(result)
    return {k: v for d in payload_batches for k, v in d.items()}