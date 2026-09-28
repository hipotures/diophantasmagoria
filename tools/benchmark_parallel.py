#!/usr/bin/env python3
"""Full-cache worker sweep; defaults to campaign-g1 and five million logical tiles.

Pass --db Experiments/g1-20m.roots.jsonl to reuse the existing cache.
All configurations, counters, final cursors and timing components are retained.
"""
from benchmark_pilot import main

if __name__ == "__main__":
    main(production=True)
