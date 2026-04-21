#!/usr/bin/env python3
"""Print k where 2**k is the smallest power of two strictly greater than N."""

from __future__ import annotations

import argparse


def exponent_smallest_power_of_2_greater_than(n: int) -> int:
    """Smallest k >= 0 with 2**k > n (for negative n, result is 0 since 2**0 == 1 > n)."""
    p = 1
    k = 0
    while p <= n:
        p <<= 1
        k += 1
    return k


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "n",
        type=int,
        help="Find smallest k with 2**k > n",
    )
    parser.add_argument(
        "--value",
        action="store_true",
        help="Also print 2**k (second line)",
    )
    args = parser.parse_args()

    k = exponent_smallest_power_of_2_greater_than(args.n)
    print(k)
    if args.value:
        print(1 << k)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
