#!/usr/bin/env python3
import subprocess
import json
import os
import sys
import re
from collections import defaultdict
from datetime import datetime

# Configuration
D1_DATABASE = 'nuvio-recomendacoes'
TRIAGE_ISSUE = 192
OWNER = 'iqui27'
REPO = 'nuvio-native-legacy'
EXCLUDE_PERSON = 'trakt:iqui27'

# Known patterns to detect in logs (pattern_name, regex)
PATTERNS = [
    ('crash-wasm', r'Aborted\(|RuntimeError:'),
    ('session-no-goodbye', r'\[avisos\].*nao se despediu'),
    ('video-error', r'\[video\].*avplay.*error'),
    ('subtitle-ass', r'\[mkvass\]|\[legenda\].*fallback'),
    ('discarded-assembly', r'montagem descartada'),
    ('invalid-pattern', r'padrao fora da lista'),
    ('corrupted-path', r'decode falhou.*(?![/http])'),
    ('debrid-torbox', r'\[debrid\].*PLAN_RESTRICTED'),
    ('long-task', r'longtask-max=([0-9]+)'),
    ('catalog-quota', r'\[desc\].*cota'),
]

def exec_d1(sql: str) -> list:
    """Execute D1 query and return results"""
    cmd = [
        'npx', '--yes', 'wrangler', 'd1', 'execute',
        D1_DATABASE, '--remote', '--json',
        '--command', sql
    ]

    try:
        output = subprocess.check_output(cmd, stderr=subprocess.PIPE, text=True)
        # Extract JSON from output (skip prefix)
        json_start = output.find('[')
        if json_start == -1:
            print(f"No JSON in output: {output[:200]}", file=sys.stderr)
            return []

        json_str = output[json_start:]
        data = json.loads(json_str)
        if isinstance(data, list) and len(data) > 0:
            return data[0].get('results', [])
        return []
    except subprocess.CalledProcessError as e:
        if '7403' in str(e):
            print("D1 error 7403 (transient), retrying...", file=sys.stderr)
            import time
            time.sleep(2)
            return exec_d1(sql)
        print(f"D1 error: {e.stderr}", file=sys.stderr)
        return []
    except json.JSONDecodeError as e:
        print(f"JSON parse error: {e}", file=sys.stderr)
        return []

def get_last_processed_id() -> int:
    """Get the last processed log ID from triagem table"""
    result = exec_d1('SELECT MAX(ultimo_log) as max_id FROM triagem;')
    return result[0]['max_id'] if result and result[0]['max_id'] else 0

def get_new_logs(after_id: int) -> list:
    """Get new logs after the given ID"""
    sql = f"""
    SELECT id, pessoa, versao, plataforma, quando, texto, criado
    FROM registro
    WHERE id > {after_id} AND pessoa != '{EXCLUDE_PERSON}'
    ORDER BY id ASC;
    """
    return exec_d1(sql)

def extract_patterns(text: str) -> list:
    """Extract all matching patterns from log text"""
    matches = []
    for pattern_name, regex in PATTERNS:
        if re.search(regex, text, re.IGNORECASE | re.DOTALL):
            if pattern_name == 'long-task':
                task_match = re.search(r'longtask-max=([0-9]+)', text)
                if task_match and int(task_match.group(1)) > 2000:
                    matches.append(pattern_name)
            else:
                matches.append(pattern_name)
    return matches

def should_skip_log(log: dict) -> bool:
    """Skip logs that are just normal sessions"""
    texto = log.get('texto', '')

    # Skip normal end markers
    if texto == 'fim':
        return True

    # Skip normal (anterior) sessions without crash markers
    quando = log.get('quando', '')
    if '(anterior)' in quando and not '[avisos]' in texto:
        return True

    # Skip logs with only normal startup info
    if texto.startswith('0.0s') or 'userAgent' in texto:
        # These are normal app logs
        if not any(p in texto for p in ['error', 'Error', 'Abort', 'RuntimeError', '[avisos]', '[video]', '[mkvass]', '[debrid]', 'longtask-max=']):
            return True

    return False

def group_logs(logs: list) -> dict:
    """Group logs by pattern, version, platform"""
    groups = defaultdict(lambda: {
        'ocorrencias': 0,
        'pessoas': set(),
        'logs': []
    })

    for log in logs:
        if should_skip_log(log):
            continue

        patterns = extract_patterns(log['texto'])

        for pattern in patterns:
            key = f"{pattern}|{log['versao']}|{log['plataforma']}"
            groups[key]['ocorrencias'] += 1
            groups[key]['pessoas'].add(log['pessoa'])
            groups[key]['logs'].append(log)

    return groups

def get_previous_triagem() -> dict:
    """Get previous triagem state"""
    result = exec_d1('SELECT * FROM triagem ORDER BY criado DESC;')
    prev_map = {}
    for t in result:
        key = f"{t['padrao']}|{t['versao']}|{t['plataforma']}"
        prev_map[key] = t
    return prev_map

def insert_triagem_entry(entry: dict) -> bool:
    """Insert a new triagem entry"""
    sql = f"""
    INSERT INTO triagem (criado, ultimo_log, padrao, versao, plataforma, ocorrencias, pessoas, estado)
    VALUES (
        {entry['criado']},
        {entry['ultimo_log']},
        '{entry['padrao'].replace("'", "''")}',
        '{entry['versao']}',
        '{entry['plataforma']}',
        {entry['ocorrencias']},
        {entry['pessoas']},
        '{entry['estado']}'
    );
    """
    result = exec_d1(sql)
    return result is not None

def format_report(summary: list, groups: dict, prev_map: dict) -> str:
    """Format the GitHub comment with the triage report"""
    lines = [
        "## Log Triage Report",
        "",
        f"**Time**: {datetime.utcnow().isoformat()}Z",
        ""
    ]

    # Count patterns
    if summary:
        lines.append("### New and Regressed Patterns")
        lines.extend(summary)
        lines.append("")

    # Aggregated statistics
    lines.append("### Aggregated Statistics")

    # By version
    by_version = defaultdict(lambda: {'patterns': 0, 'occurrences': 0, 'people': set()})
    for key, group in groups.items():
        parts = key.split('|')
        version = parts[1] if len(parts) > 1 else 'unknown'
        by_version[version]['patterns'] += 1
        by_version[version]['occurrences'] += group['ocorrencias']
        by_version[version]['people'].update(group['pessoas'])

    for version in sorted(by_version.keys()):
        stats = by_version[version]
        lines.append(f"- **v{version}**: {stats['patterns']} pattern(s), {stats['occurrences']} occurrence(s), {len(stats['people'])} user(s)")

    lines.append("")
    lines.append("---")
    lines.append("Auto-generated by log triage routine")

    return "\n".join(lines)

def main():
    print("Starting log triage routine...", file=sys.stderr)

    # Get last processed ID
    last_id = get_last_processed_id()
    print(f"Last processed log ID: {last_id}", file=sys.stderr)

    # Get new logs
    new_logs = get_new_logs(last_id)
    print(f"Found {len(new_logs)} new logs", file=sys.stderr)

    if not new_logs:
        print("No new logs to process", file=sys.stderr)
        return 0

    # Group logs by pattern
    groups = group_logs(new_logs)
    print(f"Found {len(groups)} pattern groups", file=sys.stderr)

    if not groups:
        print("No patterns detected", file=sys.stderr)
        return 0

    # Get previous triagem state
    prev_map = get_previous_triagem()

    # Process and insert triagem entries
    now = int(datetime.utcnow().timestamp())
    max_log_id = new_logs[-1]['id']
    summary = []

    for key, group in groups.items():
        parts = key.split('|')
        pattern = parts[0]
        version = parts[1] if len(parts) > 1 else ''
        platform = parts[2] if len(parts) > 2 else ''

        prev = prev_map.get(key)
        estado = 'novo'

        if prev:
            # Check for regressions
            if prev['estado'].startswith('corrigido-em-'):
                fixed_in = prev['estado'].replace('corrigido-em-', '')
                if version >= fixed_in:
                    estado = 'regrediu'
                    summary.append(f"🔴 **REGRESSION**: `{pattern}` in v{version}/{platform} (was fixed in {fixed_in})")
                else:
                    estado = prev['estado']
            else:
                estado = 'conhecido'
        else:
            summary.append(f"🆕 **NEW**: `{pattern}` in v{version}/{platform} ({group['ocorrencias']}x, {len(group['pessoas'])} user(s))")

        # Insert triagem entry
        entry = {
            'criado': now,
            'ultimo_log': max_log_id,
            'padrao': pattern,
            'versao': version,
            'plataforma': platform,
            'ocorrencias': group['ocorrencias'],
            'pessoas': len(group['pessoas']),
            'estado': estado
        }

        if insert_triagem_entry(entry):
            print(f"Inserted triagem entry: {pattern}/{version}/{platform}", file=sys.stderr)
        else:
            print(f"Failed to insert triagem entry: {pattern}/{version}/{platform}", file=sys.stderr)

    # Format report
    report = format_report(summary, groups, prev_map)
    print("\n" + report)

    return 0 if summary else 1

if __name__ == '__main__':
    sys.exit(main())
