# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
import zlib, lz4.frame
import pybase64
import msgspec
from typing import Literal
from concurrent.futures import ThreadPoolExecutor

compressors = {
    "zlib": zlib.compress,
    "lz4": lz4.frame.compress,
}
decompressors = {
    "zlib": zlib.decompress,
    "lz4": lz4.frame.decompress,
}

def compress(
    payload,
    level: int = 1,
    engine: Literal["zlib", "lz4"] = "lz4",
    version: int = 44,
) -> str | None:
    """
    Compress a payload using either JSON (legacy, version < 44)
    or Msgpack (version >= 44), wrapped in Base64 text.
    """
    try:
        if version < 44:
            raw = msgspec.json.encode(payload)
        else:
            raw = msgspec.msgpack.encode(payload)

        compressed = compressors[engine](raw, level)
        return pybase64.b64encode(compressed).decode("ascii")
    except Exception as ex:
        print(f"Failed to compress! {ex}")
        return None


def decompress(
    payload: str | dict,
    engine: Literal["zlib", "lz4"] = "lz4",
    version: int = 44,
) -> dict | None:
    """
    Decompress payload using the correct codec depending on version.
    - version < 44 → JSON
    - version >= 44 → Msgpack
    Supports Base64-encoded transport, and old dict container format.
    """
    try:
        if isinstance(payload, str):
            decoded = pybase64.b64decode(payload)
            raw = decompressors[engine](decoded)

            if version < 44:
                return msgspec.json.decode(raw)
            return msgspec.msgpack.decode(raw)

        else:
            # Legacy container with 'payload' and 'books'
            decoded_main = pybase64.b64decode(payload["payload"])
            raw_main = decompressors[engine](decoded_main)
            if version < 44:
                decompressed_payload = msgspec.json.decode(raw_main)
            else:
                decompressed_payload = msgspec.msgpack.decode(raw_main)

            if payload.get("books"):
                decoded_books = pybase64.b64decode(payload["books"])
                raw_books = decompressors[engine](decoded_books)
                if version < 44:
                    books = msgspec.json.decode(raw_books)
                else:
                    books = msgspec.msgpack.decode(raw_books)
            else:
                books = {}

            return {"books": books, **decompressed_payload}

    except Exception as ex:
        print(f"Failed to decompress! {ex}")
        return None


def compress_batch(payloads: dict, level: int = 1, engine: str = "lz4", version: int = 44) -> dict:
    return {uid: compress(payload, level=level, engine=engine, version=version) for uid, payload in payloads.items()}


def batch_compress(
    payloads: dict,
    batches: list[list[int]],
    level: int = 1,
    engine: str = "lz4",
    version: int = 44,
) -> dict:
    payload_batches = []
    with ThreadPoolExecutor(max_workers=len(batches)) as pool:
        tasks = [
            pool.submit(compress_batch, {uid: payloads[uid] for uid in batch}, level, engine, version)
            for batch in batches
        ]
        for task in tasks:
            payload_batches.append(task.result())
    return {k: v for d in payload_batches for k, v in d.items()}
