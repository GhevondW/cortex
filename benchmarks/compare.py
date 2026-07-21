#!/usr/bin/env python3
"""Compare two cortex_bench --csv outputs and fail on regressions.

Usage: compare.py base.csv head.csv [--max-regression 1.30]

A benchmark fails the check when head_ns > base_ns * max_regression.
The tolerance absorbs shared-runner noise; both sides must have been
measured on the same machine for the comparison to mean anything.
"""

import argparse
import csv
import sys


def load(path):
    with open(path, newline="") as f:
        return {row[0]: float(row[1]) for row in csv.reader(f) if row}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base")
    parser.add_argument("head")
    parser.add_argument("--max-regression", type=float, default=1.30)
    args = parser.parse_args()

    base = load(args.base)
    head = load(args.head)

    failures = []
    print(f"{'benchmark':<44} {'base ns':>10} {'head ns':>10} {'ratio':>7}")
    for name, head_ns in head.items():
        base_ns = base.get(name)
        if base_ns is None:
            print(f"{name:<44} {'new':>10} {head_ns:>10.1f} {'-':>7}")
            continue
        ratio = head_ns / base_ns if base_ns > 0 else float("inf")
        marker = ""
        if ratio > args.max_regression:
            marker = "  << REGRESSION"
            failures.append(name)
        print(f"{name:<44} {base_ns:>10.1f} {head_ns:>10.1f} {ratio:>6.2f}x{marker}")

    if failures:
        print(
            f"\nFAIL: {len(failures)} benchmark(s) regressed more than "
            f"{(args.max_regression - 1) * 100:.0f}%: {', '.join(failures)}"
        )
        return 1

    print("\nOK: no benchmark regressed beyond the tolerance.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
