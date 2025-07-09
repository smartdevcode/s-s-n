# SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
# SPDX-License-Identifier: MIT
def duration_from_timestamp(timestamp : int) -> str:
    seconds, nanoseconds = divmod(timestamp, 1_000_000_000)
    minutes, seconds = divmod(seconds, 60)
    hours, minutes = divmod(minutes, 60)
    days, hours = divmod(hours, 24)
    return (f"{days}d " if days > 0 else "") + f"{hours:02}:{minutes:02}:{seconds:02}.{nanoseconds:09d}"
        
def normalize(lower, upper, value):
    return (max(min(value, upper), lower) + upper) / (upper - lower)