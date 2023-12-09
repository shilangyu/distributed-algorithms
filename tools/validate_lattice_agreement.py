#!/usr/bin/env python3

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Generator


@dataclass
class Config:
    file_name: str
    p: int
    vs: int
    ds: int
    proposals: list[list[int]]

    @staticmethod
    def from_file(path: Path):
        with open(path, "r") as f:
            lines = [l[:-1] for l in f.readlines()]
            match lines:
                case [header, *proposals]:
                    match header.split(" "):
                        case [p, vs, ds]:
                            return Config(
                                str(path),
                                int(p),
                                int(vs),
                                int(ds),
                                [list(map(int, x.split(" "))) for x in proposals],
                            )
                        case _:
                            raise argparse.ArgumentTypeError(
                                f"Config file `{path}` header is missing p, vs, or ds"
                            )
                case _:
                    raise argparse.ArgumentTypeError(
                        f"Config file `{path}` is missing a header"
                    )


@dataclass
class Output:
    file_name: str
    decide_sets: list[list[int]]

    @staticmethod
    def from_file(path: Path):
        with open(path, "r") as f:
            lines = [l[:-1] for l in f.readlines()]
            return Output(str(path), [list(map(int, x.split(" "))) for x in lines])


def file_exists(value: str) -> Path:
    path = Path(value)
    if not path.exists():
        raise argparse.ArgumentTypeError(f"`{value}` does not exist")
    if not path.is_file():
        raise argparse.ArgumentTypeError(f"`{value}` is not a file")
    return path


def zip_union(p: int, items: list[list[list[int]]]) -> Generator[set[int], None, None]:
    for i in range(p):
        values: set[int] = set()
        for j in range(len(items)):
            values |= set(items[j][i])
        yield values


def check_configs(configs: list[Config]):
    params = set((x.p, x.vs, x.ds) for x in configs)
    if len(params) > 1:
        raise SyntaxError("Config files do not have the same header")

    (p, vs, ds) = params.pop()

    for config in configs:
        if len(config.proposals) != p:
            raise SyntaxError(
                f"Config file {config.file_name}: the amount of proposals is not equal to `p`"
            )

    for config in configs:
        for n, proposal in enumerate(config.proposals):
            if len(proposal) > vs:
                raise SyntaxError(
                    f"Config file {config.file_name}, proposal nr {n+1}: the amount of values exceeds `vs`"
                )

    for n, proposal in enumerate(zip_union(p, [x.proposals for x in configs])):
        if len(proposal) > ds:
            raise SyntaxError(
                f"Proposal nr {n+1}: there are over `ds` unique values among all processes"
            )


def check_outputs(configs: list[Config], outputs: list[Output]):
    p = configs[0].p

    for output in outputs:
        # property: we decide not more times than the amount of agreements
        if len(output.decide_sets) > p:
            raise SyntaxError(
                f"Output file {output.file_name}: there are more decide sets than proposals"
            )

        for n, decided_set in enumerate(output.decide_sets):
            # property: we decide unique values
            if len(decided_set) != len(set(decided_set)):
                raise SyntaxError(
                    f"Output file {output.file_name}, agreement nr {n+1}: the decided set contains duplicate values"
                )

    unique = list(zip_union(p, [x.proposals for x in configs]))

    for config, output in zip(configs, outputs):
        for n, (proposed, decided) in enumerate(
            zip(config.proposals, output.decide_sets)
        ):
            # property: self-validity I ⊆ O
            if not set(proposed).issubset(set(decided)):
                raise SyntaxError(
                    f"Output file {output.file_name}, agreement nr {n+1}: the decided set is not a superset of the proposed set of this process"
                )
            # property: global-validity O ⊆ union(I_j)
            if not set(decided).issubset(unique[n]):
                raise SyntaxError(
                    f"Output file {output.file_name}, agreement nr {n+1}: the decided set is not a subset of the union of all proposed sets"
                )

    for output1 in outputs:
        for output2 in outputs:
            for n, (decided1, decided2) in enumerate(
                zip(output1.decide_sets, output2.decide_sets)
            ):
                # property: consistency O1 ⊆ O2 or O2 ⊆ O1
                if not (
                    set(decided1).issubset(set(decided2))
                    or set(decided2).issubset(set(decided1))
                ):
                    raise SyntaxError(
                        f"Output files {output1.file_name} and {output2.file_name}, agreement nr {n+1}: none of the decided sets are a subset of eachother"
                    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Passed config and output files have to be in respective order of a process"
    )

    parser.add_argument(
        "--configs",
        required=True,
        type=lambda x: Config.from_file(file_exists(x)),
        nargs="+",
        dest="configs",
        help="Config files for lattice agreement",
    )
    parser.add_argument(
        "--outputs",
        required=True,
        type=lambda x: Output.from_file(file_exists(x)),
        nargs="+",
        dest="outputs",
        help="Output files",
    )

    results = parser.parse_args()

    if len(results.configs) != len(results.outputs):
        raise argparse.ArgumentTypeError(
            "Got different amout of config and output files"
        )

    try:
        check_configs(results.configs)
        check_outputs(results.configs, results.outputs)
    except SyntaxError as err:
        print(err.msg, file=sys.stderr)
        exit(1)
