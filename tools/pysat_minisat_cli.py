"""MiniSat-compatible CLI backed by PySAT's CaDiCaL 195 (optional dependency)."""

import sys

from pysat.formula import CNF
from pysat.solvers import Solver


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: pysat_minisat_cli.py input.cnf output.model", file=sys.stderr)
        return 1
    cnf = CNF(from_file=sys.argv[1])
    with Solver(name="cadical195", bootstrap_with=cnf.clauses) as solver:
        satisfiable = solver.solve()
        with open(sys.argv[2], "w", encoding="ascii") as output:
            if satisfiable:
                output.write("SAT\n")
                output.write(" ".join(map(str, solver.get_model())) + " 0\n")
            else:
                output.write("UNSAT\n")
        return 10 if satisfiable else 20


if __name__ == "__main__":
    sys.exit(main())
