#!/usr/bin/env python3
"""Automatic log triage routine.

Reads new logs from D1 registro table since the last triage,
aggregates them by pattern/version/platform, updates the triagem table,
and comments on the GitHub issue with aggregated results.

Usage:
  python3 tools/triagem.py                 # Full run
  python3 tools/triagem.py --dry-run       # Show what would happen
"""
import argparse
import json
import os
import re
import subprocess
import sys
from collections import defaultdict
from datetime import datetime, timezone
from time import time

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVIDOR = os.path.join(RAIZ, "servidor", "recomendacoes")

# Known patterns
PATTERNS = {
    "Aborted": "crash (WASM)",
    "RuntimeError": "crash (WASM)",
    "nao se despediu": "session didn't close",
    "[video]": "video issue",
    "[avplay]": "avplay error",
    "[mkvass]": "subtitle ASS",
    "[legenda]": "subtitle",
    "montagem descartada": "assembly discarded",
    "[debrid]": "debrid issue",
    "longtask-max=": "long task",
    "[desc]": "desc issue",
    "decode falhou": "decode failed",
    "diagnostico=v2": "diagnostics v2",
}

EXCLUDES = [
    "trakt:iqui27",  # Owner
]


def wrangler_execute(sql):
    """Execute SQL via wrangler, return parsed results."""
    cmd = ["npx", "--yes", "wrangler", "d1", "execute", "nuvio-recomendacoes",
           "--remote", "--json", "--command", sql]
    p = subprocess.run(cmd, cwd=SERVIDOR, capture_output=True, text=True, timeout=180)
    if p.returncode != 0:
        ult = (p.stderr.strip().splitlines() or ["?"])[-1]
        sys.exit(f"wrangler failed: {ult[:200]}")
    # Output has noise before JSON array
    saida = p.stdout[p.stdout.find("["):]
    if not saida:
        sys.exit("wrangler: no JSON output")
    return json.loads(saida)[0]["results"]


def get_last_triagem_id():
    """Get the last logged record ID from triagem table."""
    sql = "SELECT MAX(ultimo_log) as max_id FROM triagem"
    results = wrangler_execute(sql)
    if results and results[0].get("max_id"):
        return results[0]["max_id"]
    return 0


def get_new_logs(since_id):
    """Get all new registro entries since given ID."""
    sql = f"""SELECT id, pessoa, versao, plataforma, quando, texto, criado
              FROM registro
              WHERE id > {since_id} AND pessoa NOT IN ({', '.join(repr(e) for e in EXCLUDES)})
              ORDER BY id"""
    return wrangler_execute(sql)


def parse_pattern(texto):
    """Extract pattern from log text."""
    if not texto:
        return "unknown"

    # Check for specific patterns
    for marker, pattern_name in PATTERNS.items():
        if marker in texto:
            return pattern_name

    # Extract first meaningful line
    lines = texto.split('\n')
    for line in lines:
        if line.strip() and not line.startswith('['):
            return line[:50]

    return "unknown"


def aggregate_logs(logs):
    """Group logs by (pattern, version, platform)."""
    agg = defaultdict(lambda: {"ocorrencias": 0, "pessoas": set(), "last_id": 0})

    for log in logs:
        pattern = parse_pattern(log.get("texto", ""))
        version = log.get("versao", "")
        platform = log.get("plataforma", "")
        key = (pattern, version, platform)

        agg[key]["ocorrencias"] += 1
        agg[key]["pessoas"].add(log.get("pessoa", ""))
        agg[key]["last_id"] = max(agg[key]["last_id"], log.get("id", 0))

    return agg


def update_triagem_table(aggregated, dry_run=False):
    """Insert new triagem records."""
    if dry_run:
        print("DRY RUN: Would insert the following triagem records:")
        for (pattern, version, platform), data in aggregated.items():
            print(f"  - {pattern} | {version} | {platform}: {data['ocorrencias']} occurrences, {len(data['pessoas'])} users")
        return

    now = int(time())
    for (pattern, version, platform), data in aggregated.items():
        pessoas = len(data["pessoas"])
        ocorrencias = data["ocorrencias"]
        ultimo_log = data["last_id"]

        sql = f"""INSERT INTO triagem (criado, ultimo_log, padrao, versao, plataforma, ocorrencias, pessoas, estado)
                  VALUES ({now}, {ultimo_log}, {repr(pattern)}, {repr(version)}, {repr(platform)}, {ocorrencias}, {pessoas}, 'novo')"""

        try:
            wrangler_execute(sql)
        except Exception as e:
            print(f"Warning: Failed to insert triagem record for {pattern}: {e}")


def format_comment(aggregated, total_logs):
    """Format aggregated results as GitHub comment."""
    if not aggregated:
        return "✅ No new logs to report."

    # Group by pattern for summary
    by_pattern = defaultdict(lambda: {"ocorrencias": 0, "pessoas": set(), "versions": set(), "platforms": set()})

    for (pattern, version, platform), data in aggregated.items():
        by_pattern[pattern]["ocorrencias"] += data["ocorrencias"]
        by_pattern[pattern]["pessoas"].update(data["pessoas"])
        by_pattern[pattern]["versions"].add(version)
        by_pattern[pattern]["platforms"].add(platform)

    # Sort by frequency
    sorted_patterns = sorted(by_pattern.items(), key=lambda x: x[1]["ocorrencias"], reverse=True)

    comment = f"""## 📊 Log Triage Report

**Date**: {datetime.now(timezone.utc).strftime('%Y-%m-%d')} | **Logs Processed**: {total_logs} new records

### Top Issues Found

"""

    for i, (pattern, data) in enumerate(sorted_patterns[:10], 1):
        ocorrencias = data["ocorrencias"]
        pessoas = len(data["pessoas"])
        versions = ", ".join(sorted(data["versions"]))
        platforms = ", ".join(sorted(data["platforms"]))

        comment += f"""{i}. **{pattern}** ({ocorrencias} occurrences) — {pessoas} users
   - Versions: {versions}
   - Platforms: {platforms}

"""

    comment += f"""**Total**: {total_logs} new occurrences

---

_Generated automatically by triage routine_"""

    return comment


def save_comment_to_file(comment, dry_run=False):
    """Save comment to file for later posting via MCP tools."""
    if dry_run:
        print("DRY RUN: Would save comment to file:")
        print(comment)
        return

    import json
    output_file = os.path.join(RAIZ, ".claude", "triagem-comment.json")
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    with open(output_file, "w") as f:
        json.dump({"comment": comment, "issue": 166}, f)
    print(f"Saved comment to {output_file}")


def main():
    parser = argparse.ArgumentParser(description="Automatic log triage")
    parser.add_argument("--dry-run", action="store_true", help="Show what would happen without making changes")
    args = parser.parse_args()

    print("Starting log triage routine...")

    # Get last triaged ID
    last_id = get_last_triagem_id()
    print(f"Last triage at record ID: {last_id}")

    # Get new logs
    new_logs = get_new_logs(last_id)
    print(f"Found {len(new_logs)} new logs")

    if not new_logs:
        print("No new logs to process")
        return

    # Aggregate
    aggregated = aggregate_logs(new_logs)
    print(f"Aggregated into {len(aggregated)} patterns")

    # Update triagem table
    update_triagem_table(aggregated, dry_run=args.dry_run)

    # Save comment for posting via MCP
    comment = format_comment(aggregated, len(new_logs))
    save_comment_to_file(comment, dry_run=args.dry_run)

    print("Done!")


if __name__ == "__main__":
    main()
