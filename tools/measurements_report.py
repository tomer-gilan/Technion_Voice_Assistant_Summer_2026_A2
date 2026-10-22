"""Turns the measurement lines (step 29 on) in saved ESP32 logs into CSV tables and a summary.

Reads every @@BOOT_TIMING, @@TIMING and @@HEAP line (see ESP32/measurement_utils.h)
from one or more log files - the serial_logger.ps1 output or a log saved from the
laptop app, timestamps and all - and writes:
  boot_timing.csv  one row per boot
  timing.csv       one row per LISTEN
  heap.csv         one row per heap snapshot
  summary.md       median / min / max / p90 per numeric field, for the slides
into --out (default: a "measurements" folder next to the first log).

Step 30's hands-free LISTENs (mode=vad) are summarized apart from the manual
ones, with one derived field: first_audio_after_speech_ms, from the moment the
speaker stopped talking to the reply's first audio (eos_to_commit_ms +
first_audio_after_rec_ms) - the wait the user actually feels.

Usage:
  python tools/measurements_report.py tools/esp32_log_2026-10-15_10-00-00.txt [more logs...] [--out DIR]
"""

import argparse
import csv
import re
import statistics
import sys
from pathlib import Path

MISSING = -1  # the firmware's "this event never happened" value
# A status line: "@@TAG rest", alone or after the loggers' "[HH:MM:SS.mmm] " stamp - not an
# "@@" inside a sentence (step 29 printed "Check the @@BOOT_TIMING and @@TIMING lines above").
STATUS_LINE = re.compile(r"^(?:\[[^\]]*\]\s*)?(@@\S+)\s?(.*)$")


def parse_key_values(text):
    """Splits 'a=1 b=x' into {'a': '1', 'b': 'x'}; a bare word without '=' is ignored."""
    fields = {}
    for token in text.split():
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    return fields


def parse_heap(text):
    """@@HEAP keeps its stage label last, since the label may contain spaces."""
    head, _, stage = text.partition(" stage=")
    fields = parse_key_values(head)
    fields["stage"] = stage.strip()
    return fields


def add_speech_to_audio(turn):
    """Adds first_audio_after_speech_ms to a hands-free turn that has both of its parts."""
    try:
        end_of_speech_to_commit = float(turn.get("eos_to_commit_ms", ""))
        commit_to_audio = float(turn.get("first_audio_after_rec_ms", ""))
    except ValueError:
        return
    if turn.get("mode") == "vad" and end_of_speech_to_commit != MISSING and commit_to_audio != MISSING:
        turn["first_audio_after_speech_ms"] = f"{end_of_speech_to_commit + commit_to_audio:g}"


def read_logs(paths):
    """Returns (boots, turns, heaps), each a list of dicts, in log order."""
    boots, turns, heaps = [], [], []
    for path in paths:
        for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            match = STATUS_LINE.match(line)
            if not match:
                continue
            tag, rest = match.groups()
            source = {"log": path.name, "line": line_number}
            if tag == "@@BOOT_TIMING":
                boots.append({**source, **parse_key_values(rest)})
            elif tag == "@@TIMING":
                turn = {**source, **parse_key_values(rest)}
                add_speech_to_audio(turn)
                turns.append(turn)
            elif tag == "@@HEAP":
                heaps.append({**source, **parse_heap(rest)})
    return boots, turns, heaps


def write_csv(path, rows):
    """Writes rows with the union of their keys as columns, in first-seen order."""
    columns = []
    for row in rows:
        for key in row:
            if key not in columns:
                columns.append(key)
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def numeric_values(rows, key):
    """The key's values that parse as numbers, without the firmware's -1 'never happened'."""
    values = []
    for row in rows:
        try:
            value = float(row.get(key, ""))
        except ValueError:
            continue
        if value != MISSING:
            values.append(value)
    return values


def percentile(values, fraction):
    """Nearest-rank percentile of a non-empty list."""
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, round(fraction * len(ordered) + 0.5) - 1))
    return ordered[index]


def summary_table(title, rows, skip_keys):
    """Markdown table: one line per numeric key, with count / median / min / max / p90."""
    lines = [f"## {title} ({len(rows)} rows)", "", "| field | n | median | min | max | p90 |", "|---|---|---|---|---|---|"]
    keys = []
    for row in rows:
        for key in row:
            if key not in keys and key not in skip_keys:
                keys.append(key)
    for key in keys:
        values = numeric_values(rows, key)
        if not values:
            continue
        lines.append(
            f"| {key} | {len(values)} | {statistics.median(values):g} | {min(values):g} | {max(values):g} "
            f"| {percentile(values, 0.9):g} |"
        )
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", type=Path, help="saved ESP32 log files")
    parser.add_argument("--out", type=Path, help="output folder (default: measurements/ next to the first log)")
    arguments = parser.parse_args()

    missing = [path for path in arguments.logs if not path.is_file()]
    if missing:
        sys.exit(f"Not a file: {', '.join(str(path) for path in missing)}")
    out_dir = arguments.out or arguments.logs[0].parent / "measurements"
    out_dir.mkdir(parents=True, exist_ok=True)

    boots, turns, heaps = read_logs(arguments.logs)
    if not (boots or turns or heaps):
        sys.exit("No @@BOOT_TIMING, @@TIMING or @@HEAP lines found - was the board running step 29 or later?")

    write_csv(out_dir / "boot_timing.csv", boots)
    write_csv(out_dir / "timing.csv", turns)
    write_csv(out_dir / "heap.csv", heaps)

    passed = [turn for turn in turns if turn.get("result") == "pass"]
    hands_free = [turn for turn in passed if turn.get("mode") == "vad"]
    others = [turn for turn in passed if turn.get("mode") != "vad"]  # manual, or step 29 (no mode)
    with_tool = [turn for turn in others if turn.get("tool", "-") != "-"]
    without_tool = [turn for turn in others if turn.get("tool", "-") == "-"]
    skip = {"log", "line", "n", "result", "tool", "mode"}
    manual = "manual " if hands_free else ""
    sections = [
        "# Measurements",
        "",
        f"Logs: {', '.join(path.name for path in arguments.logs)}",
        f"LISTEN cycles: {len(turns)} ({len(passed)} passed, {len(turns) - len(passed)} failed). "
        "Times in ms; *_after_rec_ms counts from the end of recording (for a hands-free LISTEN: the commit, "
        "eos_to_commit_ms after the speaker stopped). -1 values are left out.",
        "",
        summary_table("Boot", boots, skip),
        summary_table(f"Passed {manual}LISTENs, no tool call", without_tool, skip),
        summary_table(f"Passed {manual}LISTENs with a tool call", with_tool, skip),
    ]
    if hands_free:
        sections.append(summary_table("Passed hands-free LISTENs (tool calls included)", hands_free, skip))
    heap_by_stage = {}
    for heap in heaps:
        heap_by_stage.setdefault(heap["stage"], []).append(heap)
    sections.append("## Heap by stage (median bytes)\n\n| stage | n | free | largest block | lowest so far |\n|---|---|---|---|---|")
    for stage, rows in heap_by_stage.items():
        cells = [f"{statistics.median(numeric_values(rows, key)):g}" for key in ("free", "largest", "lowest")]
        sections.append(f"| {stage} | {len(rows)} | {' | '.join(cells)} |")
    (out_dir / "summary.md").write_text("\n".join(sections) + "\n", encoding="utf-8")

    print(f"{len(boots)} boots, {len(turns)} LISTEN cycles, {len(heaps)} heap snapshots -> {out_dir}")
    print((out_dir / "summary.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
