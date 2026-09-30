#!/usr/bin/env python3
"""
Automated log triage routine for Nuvio project.
Reads logs from Cloudflare D1, aggregates by pattern, and posts to GitHub.
"""

import json
import subprocess
import sys
import os
import time
from collections import defaultdict
from datetime import datetime

# Environment setup
CLOUDFLARE_ACCOUNT_ID = os.environ.get("CLOUDFLARE_ACCOUNT_ID")
CLOUDFLARE_API_TOKEN = os.environ.get("CLOUDFLARE_API_TOKEN")
REPO = "iqui27/nuvio-native-legacy"
DB_NAME = "nuvio-recomendacoes"

def run_wrangler_command(sql_command, timeout=30):
    """Execute a D1 SQL command via wrangler with retry logic."""
    cmd = [
        "npx", "--yes", "wrangler", "d1", "execute",
        DB_NAME, "--remote", "--json",
        "--command", sql_command
    ]

    env = os.environ.copy()
    env["CLOUDFLARE_ACCOUNT_ID"] = CLOUDFLARE_ACCOUNT_ID
    env["CLOUDFLARE_API_TOKEN"] = CLOUDFLARE_API_TOKEN

    retries = 4
    delays = [2, 4, 8, 16]  # exponential backoff

    for attempt in range(retries):
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env)
            if result.returncode != 0:
                if "7403" in result.stderr or attempt < retries - 1:
                    if attempt < retries - 1:
                        delay = delays[attempt]
                        print(f"Retrying after {delay}s (attempt {attempt + 1}/{retries})", file=sys.stderr)
                        time.sleep(delay)
                        continue
                print(f"Error running wrangler: {result.stderr}", file=sys.stderr)
                return None

            # Parse JSON output (skip noise before first [)
            output = result.stdout
            start = output.find('[')
            if start == -1:
                return None

            json_str = output[start:]
            data = json.loads(json_str)
            if data and len(data) > 0 and 'results' in data[0]:
                return data[0]['results']
            return []
        except subprocess.TimeoutExpired:
            if attempt < retries - 1:
                delay = delays[attempt]
                print(f"Timeout, retrying after {delay}s", file=sys.stderr)
                time.sleep(delay)
                continue
            print("Wrangler command timed out", file=sys.stderr)
            return None
        except json.JSONDecodeError as e:
            print(f"JSON parse error: {e}", file=sys.stderr)
            return None

    return None


def get_max_ultimo_log():
    """Get the maximum ultimo_log from triagem table."""
    results = run_wrangler_command("SELECT COALESCE(MAX(ultimo_log), 0) as max_id FROM triagem", timeout=10)
    if results and len(results) > 0:
        return int(results[0]['max_id'])
    return 0


def get_new_logs(max_id):
    """Get all new log records since max_id."""
    sql = f"SELECT id, pessoa, versao, plataforma, quando, texto FROM registro WHERE id > {max_id} ORDER BY id"
    return run_wrangler_command(sql, timeout=30) or []


def get_previous_triagem():
    """Get all previous triagem records."""
    sql = "SELECT padrao, versao, plataforma, estado, issue FROM triagem"
    return run_wrangler_command(sql, timeout=30) or []


def extract_pattern(log_text):
    """Extract pattern from log text."""
    if not log_text:
        return "other"

    patterns = [
        ("Aborted(, RuntimeError", "Aborted(, RuntimeError: WASM crash (Samsung)"),
        ("nao se despediu", "nao se despediu: sessão morta"),
        ("[video] avplay erro", "[video] avplay error"),
        ("[mkvass]", "[mkvass] subtitle failure (issue #92)"),
        ("[legenda]", "[legenda] subtitle failure (issue #92)"),
        ("montagem descartada", "montagem descartada"),
        ("decode falhou", "decode failed - corrupted path"),
        ("[debrid]", "[debrid] TorBox/P2P issue"),
        ("longtask-max=", "longtask-max > 2000ms: freeze"),
        ("[desc]", "[desc] catalog cutoff (issue #126)"),
        ("diagnostico=v2", "diagnostico=v2 report"),
    ]

    for marker, pattern_name in patterns:
        if marker in log_text:
            return pattern_name

    return "other"


def aggregate_logs(logs):
    """Aggregate logs by pattern, version, platform."""
    aggregation = defaultdict(lambda: {
        'count': 0,
        'people': set(),
        'log_ids': []
    })

    for log in logs:
        pattern = extract_pattern(log.get('texto', ''))
        key = (pattern, log.get('versao', ''), log.get('plataforma', ''))
        aggregation[key]['count'] += 1
        aggregation[key]['people'].add(log['pessoa'])
        aggregation[key]['log_ids'].append(log['id'])

    return aggregation


def save_triagem_bulk(aggregation, max_log_id):
    """Save aggregation results to triagem table in bulk."""
    now = int(datetime.now().timestamp())
    previous = {(t['padrao'], t['versao'], t['plataforma']): t
                for t in get_previous_triagem()}

    insert_queries = []

    for (pattern, versao, plataforma), data in aggregation.items():
        ocorrencias = data['count']
        pessoas = len(data['people'])

        key = (pattern, versao, plataforma)
        prev = previous.get(key, {})

        # Determine estado (new, known, regressed, etc)
        if prev:
            prev_estado = prev.get('estado', 'novo')
            if prev_estado.startswith('corrigido-em-'):
                # Check if this is a regression
                fixed_version = prev_estado.split('-')[-1]
                if versao >= fixed_version:
                    estado = 'regrediu'
                else:
                    estado = prev_estado
            else:
                estado = prev_estado
        else:
            estado = 'novo'

        # Escape single quotes in pattern
        pattern_esc = pattern.replace("'", "''")
        versao_esc = versao.replace("'", "''")
        plataforma_esc = plataforma.replace("'", "''")
        estado_esc = estado.replace("'", "''")

        sql = f"INSERT INTO triagem (criado, ultimo_log, padrao, versao, plataforma, ocorrencias, pessoas, estado) VALUES ({now}, {max_log_id}, '{pattern_esc}', '{versao_esc}', '{plataforma_esc}', {ocorrencias}, {pessoas}, '{estado_esc}')"
        insert_queries.append(sql)

    # Execute bulk inserts (max 5 at a time to avoid timeout)
    success = True
    for i in range(0, len(insert_queries), 5):
        batch = insert_queries[i:i+5]
        for sql in batch:
            result = run_wrangler_command(sql, timeout=30)
            if result is None:
                print(f"Failed to insert triagem", file=sys.stderr)
                success = False

    return success


def format_github_comment(aggregation, max_log_id):
    """Format aggregation results as GitHub comment."""
    comment_lines = [
        "## Automated Log Triage Report",
        "",
        f"**Logs processed**: Up to ID {max_log_id}",
        ""
    ]

    # Count totals
    total_occurrences = sum(data['count'] for data in aggregation.values())
    total_people = len(set(person for data in aggregation.values() for person in data['people']))

    comment_lines.extend([
        f"**Total**: {total_occurrences} occurrences across {total_people} users",
        "",
        "### Issues by Pattern",
        ""
    ])

    # Sort by pattern name
    sorted_items = sorted(aggregation.items(), key=lambda x: x[0][0])

    for (pattern, versao, plataforma), data in sorted_items:
        if data['count'] > 0:
            comment_lines.append(f"- **{pattern}**")
            comment_lines.append(f"  - Version: {versao or 'unknown'} | Platform: {plataforma or 'unknown'}")
            comment_lines.append(f"  - Occurrences: {data['count']} | Users: {len(data['people'])}")
            comment_lines.append("")

    return "\n".join(comment_lines)


def main():
    """Main triage routine."""
    print("Starting log triage routine...")

    # Get maximum log ID already processed
    max_id = get_max_ultimo_log()
    print(f"Last processed log ID: {max_id}")

    # Get new logs
    new_logs = get_new_logs(max_id)
    if not new_logs:
        print("No new logs to process")
        return 0

    print(f"Processing {len(new_logs)} new logs")

    # Aggregate logs by pattern
    aggregation = aggregate_logs(new_logs)

    # Save results to database
    if new_logs:
        max_log_id = max(log['id'] for log in new_logs)
        if not save_triagem_bulk(aggregation, max_log_id):
            print("Failed to save some triagem results", file=sys.stderr)

    # Format and print comment for GitHub
    comment = format_github_comment(aggregation, max_log_id)
    print("\n" + comment)

    return 0


if __name__ == "__main__":
    sys.exit(main())
