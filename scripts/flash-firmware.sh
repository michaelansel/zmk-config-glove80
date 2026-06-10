#!/usr/bin/env bash
# Auto-flash Glove80 firmware from the latest successful GitHub Actions build.
#
# Usage:
#   ./flash-firmware.sh            # flash per-device firmware (left, right, dongle)
#   ./flash-firmware.sh --reset    # flash settings-reset firmware instead
#
# Requirements: gh (GitHub CLI), authenticated with repo read access.
#   brew install gh && gh auth login

set -euo pipefail

REPO="michaelansel/zmk-config-glove80"
ARTIFACT_NAME="glove80-dongle-firmware"

# Bootloader volume → firmware file mappings (VOL:FILE pairs, bash 3.2 compatible)
VOLUME_MAP_NORMAL=(
  "GLV80LHBOOT:glove80_left.uf2"
  "GLV80RHBOOT:glove80_right.uf2"
  "NICENANO:glove80_dongle.uf2"
)
VOLUME_MAP_RESET=(
  "GLV80LHBOOT:glove80_left_reset.uf2"
  "GLV80RHBOOT:glove80_right_reset.uf2"
  "NICENANO:glove80_dongle_reset.uf2"
)

# How long (seconds) to keep watching after the last device was flashed,
# or from startup if no device has appeared yet.
IDLE_TIMEOUT=120

# ── helpers ───────────────────────────────────────────────────────────────────
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }
green() { printf '\033[1;32m%s\033[0m\n' "$*"; }
red()   { printf '\033[1;31m%s\033[0m\n' "$*"; }

die() { red "Error: $*"; exit 1; }

# ── argument parsing ──────────────────────────────────────────────────────────
USE_RESET=0
for arg in "$@"; do
  case "$arg" in
    --reset) USE_RESET=1 ;;
    *) die "Unknown argument: $arg" ;;
  esac
done

if (( USE_RESET )); then
  VOLUME_MAP=("${VOLUME_MAP_RESET[@]}")
else
  VOLUME_MAP=("${VOLUME_MAP_NORMAL[@]}")
fi

# Derive the ordered list of volumes to watch from the active map
BOOTLOADER_VOLUMES=()
for _pair in "${VOLUME_MAP[@]}"; do
  BOOTLOADER_VOLUMES+=("${_pair%%:*}")
done
unset _pair

firmware_for_vol() {
  local vol="$1" pair
  for pair in "${VOLUME_MAP[@]}"; do
    [ "${pair%%:*}" = "$vol" ] && echo "${pair#*:}" && return 0
  done
  return 1
}

# ── dependency check ──────────────────────────────────────────────────────────
command -v gh &>/dev/null || die "'gh' (GitHub CLI) not found. Install with: brew install gh"

# ── work dir ──────────────────────────────────────────────────────────────────
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# ── step 1: find latest successful run ───────────────────────────────────────
bold "=== Glove80 Firmware Auto-Flasher ==="
echo
echo "Fetching latest successful build..."

RUN_ID=$(gh run list \
  --repo "$REPO" \
  --workflow build.yml \
  --status success \
  --limit 1 \
  --json databaseId \
  --jq '.[0].databaseId')

[ -n "$RUN_ID" ] || die "No successful build found in $REPO"

RUN_META=$(gh run view "$RUN_ID" \
  --repo "$REPO" \
  --json headSha,createdAt,displayTitle \
  --jq '"  Commit:  " + .headSha[:8] + "\n  Built:   " + .createdAt + "\n  Message: " + .displayTitle')

echo "  Run ID: $RUN_ID"
echo "$RUN_META"
echo

# ── step 2: download artifact ─────────────────────────────────────────────────
echo "Downloading '$ARTIFACT_NAME'..."
GH_NO_UPDATE_NOTIFIER=1 gh run download "$RUN_ID" \
  --repo "$REPO" \
  --name "$ARTIFACT_NAME" \
  --dir "$WORK_DIR"

# Verify and display each expected firmware file
echo
MISSING=0
for pair in "${VOLUME_MAP[@]}"; do
  VOL="${pair%%:*}"
  FW="${pair#*:}"
  FW_PATH="$WORK_DIR/$FW"
  if [ ! -f "$FW_PATH" ]; then
    red "  Missing: $FW"
    MISSING=$(( MISSING + 1 ))
  else
    SIZE_KB=$(( $(stat -f%z "$FW_PATH") / 1024 ))
    printf "  %-20s → %s (%d KB)\n" "$VOL" "$FW" "$SIZE_KB"
  fi
done
(( MISSING == 0 )) || die "$MISSING firmware file(s) missing from artifact"
green "Firmware ready."
echo

# ── step 3: copy helpers ──────────────────────────────────────────────────────
# cp on macOS requires Terminal to have "Removable Volumes" access in
# System Preferences > Privacy & Security > Files and Folders.  If it lacks
# that, fall back to Finder via osascript, which always has the permission.
# Returns non-zero if both paths fail (caller decides how to handle).
_do_copy() {
  local src="$1" mount="$2"
  cp -X "$src" "$mount/" 2>/dev/null && return 0
  local vol_name
  vol_name=$(basename "$mount")
  osascript -e "tell application \"Finder\" to duplicate POSIX file \"$src\" to disk \"$vol_name\" with replacing" 2>/dev/null
}

# The UF2 bootloader reboots as soon as it has received all its blocks, which
# hard-ejects the volume before the copy command can return.  We need to:
#   1. sleep briefly so the freshly-mounted volume is ready for writes
#   2. run the copy in a background process GROUP (set -m) so that
#      kill -- -$pid kills both the bash subshell AND its cp child together,
#      preventing an orphaned cp from printing I/O errors to the terminal
flash_device() {
  local src="$1" mount="$2"
  sleep 0.5  # let the volume finish mounting before writing
  set -m
  _do_copy "$src" "$mount" &
  local pid=$!
  set +m
  while kill -0 "$pid" 2>/dev/null; do
    if [[ ! -d "$mount" ]]; then
      kill -- "-$pid" 2>/dev/null
      wait "$pid" 2>/dev/null || true
      return 0
    fi
    sleep 0.1
  done
  if ! wait "$pid"; then
    if [[ ! -d "$mount" ]]; then
      return 0  # volume gone despite copy error = device rebooted = success
    fi
    red "Copy failed. Grant Terminal 'Removable Volumes' access:"
    red "  System Preferences → Privacy & Security → Files and Folders → Terminal"
    return 1
  fi
}

# ── step 4: watch for bootloader volumes ──────────────────────────────────────
bold "Put the keyboard into bootloader mode."
echo "  Left half:  hold Magic+E while turning on the power button"
echo "  Right half: hold I+PgDn while turning on the power button"
echo "  Dongle:     hold Magic and press Esc+Quote combo"
echo
printf "Watching for: %s\n" "${BOOTLOADER_VOLUMES[*]}"
echo "(${IDLE_TIMEOUT}s idle timeout — Ctrl+C to abort)"
echo

FLASHED=""
FLASH_COUNT=0
LAST_ACTIVITY=$SECONDS

already_flashed() { case " $FLASHED " in *" $1 "*) return 0;; esac; return 1; }

while true; do
  for VOL in "${BOOTLOADER_VOLUMES[@]}"; do
    MOUNT="/Volumes/$VOL"
    if [[ -d "$MOUNT" ]] && ! already_flashed "$VOL"; then
      FW_FILE=$(firmware_for_vol "$VOL")
      FLASHED="$FLASHED $VOL"
      FLASH_COUNT=$(( FLASH_COUNT + 1 ))
      LAST_ACTIVITY=$SECONDS
      printf "  → %s detected\n" "$MOUNT"
      printf "    Flashing %s ... " "$FW_FILE"
      flash_device "$WORK_DIR/$FW_FILE" "$MOUNT"
      green "done"
      echo "    (watching for more devices — Ctrl+C when finished)"
      echo
    fi
  done

  elapsed=$(( SECONDS - LAST_ACTIVITY ))
  if (( elapsed >= IDLE_TIMEOUT )); then
    if (( FLASH_COUNT == 0 )); then
      red "Timed out after ${IDLE_TIMEOUT}s — no bootloader device appeared."
      exit 1
    else
      green "All done. Flashed $FLASH_COUNT device(s):$FLASHED"
      exit 0
    fi
  fi

  sleep 0.5
done
