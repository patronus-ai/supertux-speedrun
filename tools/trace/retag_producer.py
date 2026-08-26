#!/usr/bin/env python3
"""Retag a trace's header.producer_kind and write the result to a new file.

Nothing in this repository can produce an AGENT-tagged trace: the engine's
own recorder hardcodes ProducerKind.HUMAN (trace_recorder.cpp's
make_header()), tape_convert.py never sets it, and every probe in this
directory that needs an agent trace rewrites the field itself, in-test. The
ghost store's whole selection rule is "agent traces in, human runs out"
(ghost_store.py's store_verdict(), mirroring src/trace/ghost_store.cpp), so
without this, the headline "race the agents" story has no in-repo way to be
set up at all -- not even the eight agent runs this project has already
produced, which were all recorded as HUMAN like everything else.

Usage:
    uv run python retag_producer.py IN.stgt OUT.stgt {unknown|human|script|agent}

Deliberately three positional arguments, not two: an in-place default would
make one bad run silently destroy the only copy of a trace someone may not
be able to re-record (an agent's run, in particular -- there is no `--agent`
flag to rerun one with). Naming OUT is cheap and makes the overwrite a
decision the caller made on purpose. Passing the same path for IN and OUT is
allowed -- that IS naming an output, just choosing to replace the input.

Refuses to write anything if IN does not parse as a trace at all:
read_trace() (supertux_trace.py) raises before this script's own code runs,
and that exception is left to propagate rather than caught, so a malformed
IN produces a traceback and a non-zero exit, never a half-written OUT.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from supertux_trace import ProducerKind, read_trace, write_trace

PRODUCER_KIND_NAMES = {member.name.lower(): member for member in ProducerKind}


def main(argv: list) -> int:
    if len(argv) != 3:
        names = "|".join(PRODUCER_KIND_NAMES)
        print(f"usage: retag_producer.py IN.stgt OUT.stgt {{{names}}}",
              file=sys.stderr)
        return 2

    in_path, out_path, kind_name = argv

    try:
        kind = PRODUCER_KIND_NAMES[kind_name.lower()]
    except KeyError:
        names = "|".join(PRODUCER_KIND_NAMES)
        print(f"retag_producer.py: unknown producer kind '{kind_name}' "
              f"(expected one of {names})", file=sys.stderr)
        return 2

    # Raises (ParseError-equivalent, or whatever read_bytes/loads throws for
    # a file that is not a trace at all) rather than being caught here --
    # see this module's own docstring on why a parse failure must never
    # reach write_trace() below.
    trace = read_trace(in_path)

    trace.header.producer_kind = kind
    write_trace(out_path, trace)

    print(f"retag_producer.py: {in_path} -> {out_path} "
          f"(producer_kind={kind.name})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
