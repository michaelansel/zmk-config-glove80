#!/usr/bin/env python3
"""
fmt-keymap.py — Glove80 ZMK keymap formatter.

Maintains consistent per-column alignment across all keymap layers.
Halts with an actionable error if any binding exceeds --max-width characters.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Glove80 layout constants
# ---------------------------------------------------------------------------

# Visual row → number of binding tokens per row
ROW_LENGTHS = [10, 12, 12, 12, 18, 16]

# Physical column IDs present (real bindings) for each row token count.
# Rows 0 and 5 are missing L5 (innermost left) and R0 (innermost right).
ROW_COLS: dict[int, list[str]] = {
    10: ['L0','L1','L2','L3','L4',                                             'R1','R2','R3','R4','R5'],
    12: ['L0','L1','L2','L3','L4','L5',                                        'R0','R1','R2','R3','R4','R5'],
    18: ['L0','L1','L2','L3','L4','L5','T0','T1','T2','T3','T4','T5','R0','R1','R2','R3','R4','R5'],
    16: ['L0','L1','L2','L3','L4',      'T0','T1','T2','T3','T4','T5',         'R1','R2','R3','R4','R5'],
}

# Single column sequence used for ALL rows. Columns absent from a given row
# are emitted as phantom whitespace, so the right-hand columns always start
# at the same horizontal position regardless of whether a thumb row or not.
# This ensures e.g. H (row 3 R0) sits directly above N (row 4 R0).
FULL_COL_SEQ = ['L0','L1','L2','L3','L4','L5','T0','T1','T2','T3','T4','T5','R0','R1','R2','R3','R4','R5']

COL_GROUP: dict[str, str] = {}
for _c in 'L0 L1 L2 L3 L4 L5'.split():
    COL_GROUP[_c] = 'left'
for _c in 'T0 T1 T2 T3 T4 T5'.split():
    COL_GROUP[_c] = 'thumb'
for _c in 'R0 R1 R2 R3 R4 R5'.split():
    COL_GROUP[_c] = 'right'

ALL_COLS = 'L0 L1 L2 L3 L4 L5 T0 T1 T2 T3 T4 T5 R0 R1 R2 R3 R4 R5'.split()
KEYMAP_BINDING_COUNT = 80

# Matches exactly-6-line binding blocks (keymap layers).
# Non-keymap bindings are either single-line or use a split `bindings\n  =` form.
BLOCK_RE = re.compile(
    r'([ \t]*)bindings\s*=\s*<\n'
    r'((?:[^\n]*\n){6})'
    r'([ \t]*)>;',
    re.MULTILINE,
)

# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

def parse_bindings(line: str) -> list[str]:
    """Split a binding line into individual binding strings.

    Each binding starts with & and includes all following words until the next &.
    Handles &none (1 word), &kp N1 (2 words), &magic MAGIC 0 (3 words), etc.
    """
    tokens: list[str] = []
    cur: list[str] | None = None
    for word in line.split():
        if word.startswith('&'):
            if cur is not None:
                tokens.append(' '.join(cur))
            cur = [word]
        elif cur is not None:
            cur.append(word)
    if cur:
        tokens.append(' '.join(cur))
    return tokens


def decode_block(inner: str) -> list[list[tuple[str, str]]]:
    """Decode a binding block into 6 rows of (col_id, binding) pairs."""
    lines = inner.rstrip('\n').split('\n')
    if len(lines) != 6:
        raise ValueError(f'Expected 6 lines, got {len(lines)}')
    rows = []
    for line, n in zip(lines, ROW_LENGTHS):
        bindings = parse_bindings(line)
        if len(bindings) != n:
            raise ValueError(f'Expected {n} bindings on line, got {len(bindings)}')
        rows.append(list(zip(ROW_COLS[n], bindings)))
    return rows


def layer_name_for_offset(content: str, offset: int) -> str:
    """Find the enclosing layer name by searching backward for the nearest WORD {."""
    m = re.search(r'(\w+)\s*\{[^{]*$', content[:offset], re.DOTALL)
    return m.group(1) if m else 'unknown'


# ---------------------------------------------------------------------------
# Formatting
# ---------------------------------------------------------------------------

def compute_col_max(all_rows: list[list[list[tuple[str, str]]]]) -> dict[str, int]:
    col_max = {c: 0 for c in ALL_COLS}
    for rows in all_rows:
        for row in rows:
            for col_id, binding in row:
                if len(binding) > col_max[col_id]:
                    col_max[col_id] = len(binding)
    return col_max


def format_row(
    row_dict: dict[str, str],
    n_tokens: int,
    col_max: dict[str, int],
    indent: str,
    half_gap: int,
) -> str:
    present = set(ROW_COLS[n_tokens])
    parts: list[str] = []
    prev_group: str | None = None

    for col_id in FULL_COL_SEQ:
        grp = COL_GROUP[col_id]
        if prev_group is None:
            sep = ''
        elif grp != prev_group:
            sep = ' ' * half_gap
        else:
            sep = ' '

        w = col_max[col_id]
        if col_id in present:
            cell = row_dict[col_id].ljust(w)
        else:
            cell = ' ' * w  # phantom: occupies the column width, no binding

        parts.append(sep + cell)
        prev_group = grp

    return indent + ''.join(parts).rstrip()


def format_block(
    rows: list[list[tuple[str, str]]],
    col_max: dict[str, int],
    indent: str,
    half_gap: int,
) -> str:
    lines = []
    for row_idx, row_pairs in enumerate(rows):
        n = ROW_LENGTHS[row_idx]
        lines.append(format_row(dict(row_pairs), n, col_max, indent, half_gap))
    return '\n'.join(lines) + '\n'


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description='Format Glove80 ZMK keymap binding blocks for consistent column alignment.',
    )
    parser.add_argument('keymap', nargs='?', help='Path to .keymap file')
    parser.add_argument(
        '--max-width', type=int, default=20, metavar='N',
        help='Max binding char count before error (default: 20)',
    )
    parser.add_argument(
        '--half-gap', type=int, default=4, metavar='N',
        help='Extra spaces between keyboard half groups (default: 4)',
    )
    parser.add_argument(
        '--dry-run', action='store_true',
        help='Print formatted output to stdout, do not write file',
    )
    parser.add_argument(
        '--check', action='store_true',
        help='Exit 1 if file needs formatting (for pre-commit hooks)',
    )
    parser.add_argument(
        '--compact', action='store_true',
        help='Size each layer to its own bindings instead of a shared global grid',
    )
    args = parser.parse_args()

    if args.keymap:
        keymap_path = Path(args.keymap)
    else:
        keymap_path = Path(__file__).parent.parent / 'config' / 'glove80.keymap'

    content = keymap_path.read_text()

    # Locate and decode all 80-key binding blocks
    valid_blocks: list[tuple[re.Match, str, list[list[tuple[str, str]]]]] = []
    for m in BLOCK_RE.finditer(content):
        try:
            rows = decode_block(m.group(2))
        except ValueError:
            continue
        if sum(len(r) for r in rows) != KEYMAP_BINDING_COUNT:
            continue
        layer = layer_name_for_offset(content, m.start())
        valid_blocks.append((m, layer, rows))

    if not valid_blocks:
        print('No valid 80-key binding blocks found.', file=sys.stderr)
        sys.exit(1)

    # Check binding lengths before making any changes
    errors: list[tuple[str, int, str, str]] = []
    for _m, layer, rows in valid_blocks:
        for row_idx, row in enumerate(rows):
            for col_id, binding in row:
                if len(binding) > args.max_width:
                    errors.append((layer, row_idx, col_id, binding))

    if errors:
        for layer, row_idx, col_id, binding in errors:
            last_word = binding.rsplit(None, 1)[-1].lstrip('&')
            print(
                f"Error: {binding!r} in layer '{layer}', row {row_idx}, col {col_id} "
                f"is {len(binding)} chars (limit: {args.max_width}).",
                file=sys.stderr,
            )
            print(
                f"  Add to config/michaelansel.h:  #define {last_word}_SHORT  {last_word}",
                file=sys.stderr,
            )
            print(
                '  then replace with the shorter alias in the keymap.',
                file=sys.stderr,
            )
        sys.exit(1)

    global_col_max = compute_col_max([rows for _m, _layer, rows in valid_blocks])

    # Pre-compute formatted inner content keyed by match start position.
    formatted_by_start: dict[int, tuple[str, str]] = {
        m.start(): (
            format_block(
                rows,
                compute_col_max([rows]) if args.compact else global_col_max,
                m.group(1),
                args.half_gap,
            ),
            m.group(3),
        )
        for m, _layer, rows in valid_blocks
    }

    def replacer(m: re.Match) -> str:  # type: ignore[type-arg]
        entry = formatted_by_start.get(m.start())
        if entry is None:
            return m.group(0)
        new_inner, close_indent = entry
        return m.group(1) + 'bindings = <\n' + new_inner + close_indent + '>;'

    new_content = BLOCK_RE.sub(replacer, content)

    if args.check:
        if new_content == content:
            sys.exit(0)
        print(
            'File needs formatting. Run: python3 scripts/fmt-keymap.py',
            file=sys.stderr,
        )
        sys.exit(1)

    if args.dry_run:
        sys.stdout.write(new_content)
        return

    tmp = keymap_path.with_suffix('.tmp')
    tmp.write_text(new_content)
    os.replace(tmp, keymap_path)
    print(f'Formatted {keymap_path} ({len(valid_blocks)} layers)')


if __name__ == '__main__':
    main()
