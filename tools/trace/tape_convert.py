#!/usr/bin/env python3
# SuperTux
# Copyright (C) 2026 Abdelrahman Madkour <abdelrahman.madkour@patronus.ai>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""Browser keyboard tapes -> action masks.

A tape is what the `patronus-ai/supertux-speedrun` harness records: a sparse
map from logical step index to the key transitions that happen at that step.
It carries no level, no seed, no viewport and no step rate, so everything
else a trace header needs has to be supplied by the caller.
"""

from typing import Dict, List

from supertux_trace import Action


class UnknownKey(Exception):
    """A tape named a key this converter does not map to an action."""


#: Browser `KeyboardEvent.key` -> the action bit it sets.
#:
#: Deliberately only the three keys the reference corpus uses. Adding a key
#: here is a claim that a tape somewhere presses it; anything else must
#: refuse, so that "we do not model this input" cannot be mistaken for "the
#: agent did nothing".
KEY_TO_ACTION: Dict[str, Action] = {
    "ArrowRight": Action.RIGHT,
    "ArrowLeft": Action.LEFT,
    "Space": Action.JUMP,
}


def tape_to_masks(tape: Dict[str, list], step_count: int) -> List[int]:
    """Expand a sparse transition tape into one action mask per step.

    `step_count` is the caller's, never inferred from the tape's last event:
    three of the eleven runs in the reference corpus carry events past the
    step their filename names, so the tape cannot be trusted to say how long
    the episode was.
    """
    transitions = {int(step): events for step, events in tape.items()}

    masks: List[int] = []
    mask = 0
    for step in range(step_count):
        for key, _code, pressed in transitions.get(step, []):
            if key not in KEY_TO_ACTION:
                raise UnknownKey(
                    f"tape step {step} names key {key!r}, which this converter "
                    f"does not map to an action; it knows "
                    f"{sorted(KEY_TO_ACTION)}. A tape using a fourth key is "
                    f"doing something this corpus was not believed to do, so "
                    f"it is refused rather than silently dropped"
                )
            bit = 1 << KEY_TO_ACTION[key]
            # In order, and at this step rather than after it: the harness
            # applies a step's events and only then ticks. Order within the
            # step is load-bearing -- a release followed by a press of the
            # same key (which the corpus does constantly) must end pressed.
            if pressed:
                mask |= bit
            else:
                mask &= ~bit
        masks.append(mask)

    return masks
