#!/usr/bin/env python3

import argparse
from pathlib import Path
from random import randint, sample

if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--p",
        required=True,
        type=int,
        dest="p"
    )
    parser.add_argument(
        "--vs",
        required=True,
        type=int,
        dest="vs"
    )
    parser.add_argument(
        "--ds",
        required=True,
        type=int,
        dest="ds"
    )
    parser.add_argument(
        "--config-files",
        required=True,
        type=Path,
        nargs="+",
        dest="config_files",
    )

    results = parser.parse_args()

    possible_values = [
        sample(range(2147483648), results.ds) for _ in range(results.p)
    ]

    for file in results.config_files:
        with open(file, 'w') as f:
            f.write('\n'.join([f'{results.p} {results.vs} {results.ds}', *[
                ' '.join(map(str, sample(possible_values[i], randint(1, results.vs)))) for i in range(results.p)
            ], '']))
