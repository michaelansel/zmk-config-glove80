#!/usr/bin/env python3
"""
fmt-keymap.py — Glove80 ZMK keymap formatter.

Formats binding blocks to match nickcoutsos/keymap-editor output:
  - Right-aligned columns (padStart), min width 7
  - Single space between columns, no gap at the keyboard split
  - Per-layer column widths (not global across layers)
  - No indentation on binding rows (padding comes from right-alignment)

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

ROW_LENGTHS = [10, 12, 12, 12, 18, 16]

ROW_COLS: dict[int, list[str]] = {
    10: ['L0','L1','L2','L3','L4',                                             'R1','R2','R3','R4','R5'],
    12: ['L0','L1','L2','L3','L4','L5',                                        'R0','R1','R2','R3','R4','R5'],
    18: ['L0','L1','L2','L3','L4','L5','T0','T1','T2','T3','T4','T5','R0','R1','R2','R3','R4','R5'],
    16: ['L0','L1','L2','L3','L4',      'T0','T1','T2','T3','T4','T5',         'R1','R2','R3','R4','R5'],
}

FULL_COL_SEQ = ['L0','L1','L2','L3','L4','L5','T0','T1','T2','T3','T4','T5','R0','R1','R2','R3','R4','R5']

ALL_COLS = FULL_COL_SEQ
KEYMAP_BINDING_COUNT = 80
MIN_COL_WIDTH = 6

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
    m = re.search(r'(\w+)\s*\{[^{]*$', content[:offset], re.DOTALL)
    return m.group(1) if m else 'unknown'

# ---------------------------------------------------------------------------
# Formatting — matches nickcoutsos/keymap-editor renderTable output
# ---------------------------------------------------------------------------

def compute_col_widths(rows: list[list[tuple[str, str]]]) -> dict[str, int]:
    """Per-layer column widths: max(MIN_COL_WIDTH, binding_len + 1) per col.

    The +1 reserves space for the trailing separator so that the separator is
    part of the cell width, matching keymap-editor's padEnd + suffix approach.
    """
    widths = {c: MIN_COL_WIDTH for c in ALL_COLS}
    for row in rows:
        for col_id, binding in row:
            w = len(binding) + 1  # +1 for separator
            if w > widths[col_id]:
                widths[col_id] = w
    return widths


def format_row(
    row_dict: dict[str, str],
    n_tokens: int,
    col_widths: dict[str, int],
) -> str:
    """Left-align each cell to its column width, separator appended to each cell.

    Matches keymap-editor's renderTable: binding.padEnd(padding) + ' ', where
    padding = max(minWidth=7, max_binding_len+1). Phantom columns fill the same
    width so rows stay aligned regardless of which keys are present.
    """
    present = set(ROW_COLS[n_tokens])
    buf = ''

    for col_id in FULL_COL_SEQ:
        w = col_widths[col_id]
        if col_id in present:
            buf += row_dict[col_id].ljust(w) + ' '
        else:
            buf += ' ' * (w + 1)  # phantom: same total width as a real cell

    return buf.rstrip()


def format_block(
    rows: list[list[tuple[str, str]]],
    col_widths: dict[str, int],
) -> str:
    lines = []
    for row_idx, row_pairs in enumerate(rows):
        n = ROW_LENGTHS[row_idx]
        lines.append(format_row(dict(row_pairs), n, col_widths))
    return '\n'.join(lines) + '\n'

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description='Format Glove80 ZMK keymap binding blocks (nickcoutsos/keymap-editor style).',
    )
    parser.add_argument('keymap', nargs='?', help='Path to .keymap file')
    parser.add_argument(
        '--max-width', type=int, default=20, metavar='N',
        help='Max binding char count before error (default: 20)',
    )
    parser.add_argument(
        '--dry-run', action='store_true',
        help='Print formatted output to stdout, do not write file',
    )
    parser.add_argument(
        '--check', action='store_true',
        help='Exit 1 if file needs formatting (for pre-commit hooks)',
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
                f"  Add to the keymap #defines:  #define {last_word}_SHORT  {last_word}",
                file=sys.stderr,
            )
            print(
                '  then replace with the shorter alias in the keymap.',
                file=sys.stderr,
            )
        sys.exit(1)

    # Build replacement map keyed by match start position.
    # Column widths are computed per-layer (not globally) to match keymap-editor.
    formatted_by_start: dict[int, tuple[str, str, str]] = {}
    for m, _layer, rows in valid_blocks:
        col_widths = compute_col_widths(rows)
        new_inner = format_block(rows, col_widths)
        formatted_by_start[m.start()] = (m.group(1), new_inner, m.group(3))

    def replacer(m: re.Match) -> str:  # type: ignore[type-arg]
        entry = formatted_by_start.get(m.start())
        if entry is None:
            return m.group(0)
        bind_indent, new_inner, close_indent = entry
        # `bindings = <` keeps its original indentation; binding rows have no
        # indentation prefix (matches keymap-editor linePrefix='').
        return bind_indent + 'bindings = <\n' + new_inner + close_indent + '>;'

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
