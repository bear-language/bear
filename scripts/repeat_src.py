#!/usr/bin/python3

# generate a large stress-test source by repeating a source file n times
# (optionally seperating each repeat into a distinct parallel module)
#
# usage:
#   ./scripts/repeat_src.py <src> <n> [-o out.br] [--mod]

import argparse
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description="repeat a source file n times")
    parser.add_argument("src", help="source file to repeat")
    parser.add_argument("n", type=int, help="number of copies")
    parser.add_argument("-o", "--out", help="output file (default: stdout)")
    parser.add_argument(
        "--mod",
        action="store_true",
        help="wrap each copy in its own `mod mod_<i> { ... }` (sibling modules)",
    )
    args = parser.parse_args()

    if args.n < 0:
        parser.error("n must be non-negative")

    with open(args.src, "r", encoding="utf-8") as f:
        src = f.read()
    if not src.endswith("\n"):
        src += "\n"

    out = open(args.out, "w", encoding="utf-8") if args.out else sys.stdout
    try:
        for i in range(args.n):
            if args.mod:
                out.write(f"mod mod_{i} {{\n")
            out.write(src)
            if args.mod:
                out.write("}\n")
            out.write("\n")
    finally:
        if out is not sys.stdout:
            out.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
