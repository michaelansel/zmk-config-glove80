#!/usr/bin/env python3
"""
Validates and maintains layer #defines in glove80.keymap.

Each layer in the keymap block must have a comment directly above it:
    // LAYERNAME N
    somename_layer {

The zero-based position of the layer must match N and must match the
corresponding #define in the // Layers section of the same file.

Usage:
    scripts/validate-layers.py          # validate only
    scripts/validate-layers.py --fix    # fix mismatches automatically
"""

import re
import sys
import argparse
from pathlib import Path

REPO_ROOT = Path(__file__).parent.parent
KEYMAP = REPO_ROOT / "config" / "glove80.keymap"


def parse_keymap_layers(text):
    """
    Returns list of (name, comment_id) in keymap order.
    Only searches within the keymap { } block.
    """
    keymap_match = re.search(r'\bkeymap\s*\{(.*)', text, re.DOTALL)
    if not keymap_match:
        raise ValueError("No keymap { block found in keymap file")

    layers = []
    for m in re.finditer(
        r'//\s+(\w+)\s+(\d+)\s*\n\s*\w+_layer\s*\{',
        keymap_match.group(1),
    ):
        layers.append((m.group(1), int(m.group(2))))
    return layers


def parse_keymap_layer_defines(text):
    """Returns list of (name, id) from the // Layers section of the keymap file."""
    layers_match = re.search(
        r'//\s*Layers\s*\n((?:#define\s+\w+\s+\d+[^\n]*\n)*)',
        text,
    )
    if not layers_match:
        raise ValueError("No '// Layers' section found in keymap file")

    defines = []
    for m in re.finditer(r'#define\s+(\w+)\s+(\d+)', layers_match.group(1)):
        defines.append((m.group(1), int(m.group(2))))
    return defines


def validate(keymap_layers, defines):
    """Returns list of error strings. Empty means all good."""
    errors = []
    defines_dict = dict(defines)
    keymap_names = [name for name, _ in keymap_layers]

    for actual_id, (name, comment_id) in enumerate(keymap_layers):
        if comment_id != actual_id:
            errors.append(
                f"Keymap comment '// {name} {comment_id}' is at position {actual_id}"
            )
        if name not in defines_dict:
            errors.append(f"Missing #define {name} {actual_id} in // Layers section")
        elif defines_dict[name] != actual_id:
            errors.append(
                f"#define {name} {defines_dict[name]} should be {actual_id}"
            )

    for name, id_ in defines:
        if name not in keymap_names:
            errors.append(
                f"#define {name} {id_} has no matching layer in keymap block"
            )

    return errors


def fix_keymap_comments(text, keymap_layers):
    """Fix layer ID values in keymap comments, preserving surrounding formatting."""
    for actual_id, (name, comment_id) in enumerate(keymap_layers):
        if comment_id != actual_id:
            text = re.sub(
                rf'(//\s+{re.escape(name)}\s+)\d+(\s*\n\s*\w+_layer\s*\{{)',
                lambda m, new_id=actual_id: f'{m.group(1)}{new_id}{m.group(2)}',
                text,
                count=1,
            )
    return text


def fix_keymap_defines(text, keymap_layers):
    """Rewrite the // Layers section in the keymap to match keymap order and IDs."""
    desired = [(name, actual_id) for actual_id, (name, _) in enumerate(keymap_layers)]
    max_name_len = max(len(name) for name, _ in desired)
    field_width = max(max_name_len + 1, 13)  # minimum 13 to match existing style

    new_section = ''.join(
        f'#define {name:<{field_width}}{id_}\n'
        for name, id_ in desired
    )

    result = re.sub(
        r'(//\s*Layers\s*\n)(?:#define\s+\w+\s+\d+[^\n]*\n)*',
        lambda m: m.group(1) + new_section,
        text,
        count=1,
    )
    if result == text:
        raise ValueError("Could not replace // Layers section in keymap")
    return result


def main():
    parser = argparse.ArgumentParser(
        description='Validate and maintain layer #defines',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument('--fix', action='store_true', help='Fix mismatches automatically')
    args = parser.parse_args()

    keymap_text = KEYMAP.read_text()

    keymap_layers = parse_keymap_layers(keymap_text)
    defines = parse_keymap_layer_defines(keymap_text)

    print("Layers (keymap order):")
    for actual_id, (name, comment_id) in enumerate(keymap_layers):
        comment_status = "ok" if comment_id == actual_id else f"comment says {comment_id}"
        print(f"  {actual_id}  {name}  [{comment_status}]")

    print("\n// Layers defines:")
    for name, id_ in defines:
        print(f"  {name} = {id_}")

    errors = validate(keymap_layers, defines)

    if not errors:
        print("\nAll checks passed.")
        return 0

    print(f"\n{len(errors)} error(s):")
    for e in errors:
        print(f"  ERROR: {e}")

    if not args.fix:
        print("\nRun with --fix to apply automatic fixes.")
        return 1

    print("\nApplying fixes...")
    new_keymap = fix_keymap_comments(keymap_text, keymap_layers)
    new_keymap = fix_keymap_defines(new_keymap, keymap_layers)

    if new_keymap != keymap_text:
        KEYMAP.write_text(new_keymap)
        print(f"  Updated {KEYMAP.relative_to(REPO_ROOT)}")

    print("Done.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
