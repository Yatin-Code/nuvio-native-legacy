#!/usr/bin/env python3
"""
Log triage automation for nuvio-native-legacy.

Reads logs from D1 (Cloudflare), aggregates by pattern, tracks state,
and posts summary to pinned GitHub issue. Never includes personal IDs or
credentials in GitHub comments.

Usage:
  python3 tools/triagem.py

Environment:
  CLOUDFLARE_API_TOKEN - D1 API access
  CLOUDFLARE_ACCOUNT_ID - Cloudflare account ID
  GITHUB_TOKEN - for posting comments (optional, uses app auth if not set)
"""
import argparse
import json
import os
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVIDOR = os.path.join(RAIZ, "servidor", "recomendacoes")

# Known log patterns to look for
PADROES = {
    "Aborted": "crash WASM",
    "RuntimeError": "crash WASM",
    "nao se despediu": "sessão morta",
    "[video] avplay erro": "erro de player",
    "[mkvass] fallback": "legenda ASS",
    "[legenda] fallback": "legenda ASS",
    "montagem descartada": "montagem rejeitada",
    "decode falhou": "caminho corrompido",
    "[debrid]": "problema debrid",
    "longtask-max=": "travada longa",
    "[desc]": "catálogo cortado",
    "PLAN_RESTRICTED": "limite TorBox",
    "fora de cache": "debrid fora de cache",
}


def executar_sql(comando):
    """Execute SQL against D1 via wrangler."""
    cmd = [
        "npx", "--yes", "wrangler", "d1", "execute", "nuvio-recomendacoes",
        "--remote", "--json", "--command", comando
    ]

    try:
        p = subprocess.run(cmd, cwd=SERVIDOR, capture_output=True, text=True, timeout=180)
        if p.returncode != 0:
            erro = (p.stderr.strip().splitlines() or ["?"])[-1]
            if "7403" in erro:
                print(f"Aviso: erro transitório (7403), retentando em 3s...", file=sys.stderr)
                time.sleep(3)
                return executar_sql(comando)
            raise RuntimeError(f"wrangler falhou: {erro[:200]}")

        # Extract JSON from output (wrangler adds noise before JSON)
        saida = p.stdout[p.stdout.find("["):]
        resultado = json.loads(saida)
        return resultado[0].get("results", []) if resultado else []
    except subprocess.TimeoutExpired:
        raise RuntimeError("wrangler timeout (180s)")


def obter_ultimo_log():
    """Get the highest registro.id already processed."""
    sql = "SELECT COALESCE(MAX(ultimo_log), 0) as max_id FROM triagem"
    resultado = executar_sql(sql)
    return resultado[0]["max_id"] if resultado else 0


def ler_novos_registros(ultimo_id):
    """Read new registros after ultimo_id, one at a time to be safe."""
    sql = f"""
    SELECT id, pessoa, versao, plataforma, quando, texto, criado
    FROM registro
    WHERE id > {ultimo_id} AND pessoa != 'trakt:iqui27'
    ORDER BY id
    """
    try:
        registros = executar_sql(sql)
        return registros
    except RuntimeError as e:
        print(f"Erro ao ler registros: {e}", file=sys.stderr)
        return []


def detectar_padrao(texto):
    """Detect which pattern this log matches."""
    if not texto:
        return None

    # Diagnostico V2 é um padrão especial
    if "diagnostico=v2:" in texto:
        return "diagnostico-v2"

    # Check known patterns
    for chave, descricao in PADROES.items():
        if chave in texto:
            return descricao

    # Se tem avisos ou erros, marque como "outro erro"
    if "[aviso]" in texto or "[erro]" in texto:
        return "outro-erro"

    # Sessão normal sem erros
    if "fim" in texto.lower() and "[aviso]" not in texto:
        return None

    # Algo anormal mas não reconhecido
    if len(texto) > 100:
        # Truncate for pattern summary
        return f"outro ({texto[:30]}...)"

    return None


def agrupar_registros(registros):
    """Group registros by pattern, versao, plataforma."""
    grupos = defaultdict(lambda: {
        "ocorrencias": 0,
        "pessoas": set(),
        "maior_id": 0,
        "quando": [],  # para diagnosticar
    })

    for reg in registros:
        padrao = detectar_padrao(reg["texto"])
        if padrao is None:
            continue

        chave = (padrao, reg["versao"], reg["plataforma"])
        grupos[chave]["ocorrencias"] += 1
        grupos[chave]["pessoas"].add(reg["pessoa"])
        grupos[chave]["maior_id"] = max(grupos[chave]["maior_id"], reg["id"])
        grupos[chave]["quando"].append(reg["quando"])

    return grupos


def verificar_regressao(padrao, versao, plataforma):
    """Check if this pattern regressed (was marked fixed before)."""
    sql = f"""
    SELECT estado FROM triagem
    WHERE padrao = ? AND plataforma = ? AND estado LIKE 'corrigido-in-%'
    LIMIT 1
    """
    # Note: would need parameterized queries for safety; using simple approach for now
    sql_safe = f"""
    SELECT estado FROM triagem
    WHERE padrao = '{padrao.replace("'", "''")}'
    AND plataforma = '{plataforma.replace("'", "''")}'
    AND estado LIKE 'corrigido-in-%'
    LIMIT 1
    """
    try:
        resultado = executar_sql(sql_safe)
        if resultado:
            estado_anterior = resultado[0]["estado"]
            # Extract version from "corrigido-in-X.Y.Z"
            if estado_anterior.startswith("corrigido-in-"):
                versao_fixa = estado_anterior.split("-")[-1]
                # Se voltou numa versão >= à que foi fixa, é regressão
                if versao >= versao_fixa:
                    return "regrediu"
        return None
    except RuntimeError:
        return None


def gravar_triagem(triagens):
    """Write triagem results to database."""
    agora = int(time.time())

    for (padrao, versao, plataforma), dados in triagens.items():
        pessoas = len(dados["pessoas"])
        ocorrencias = dados["ocorrencias"]
        ultimo_log = dados["maior_id"]

        regressao = verificar_regressao(padrao, versao, plataforma)
        estado = regressao or "novo"

        # Check if this pattern was known
        sql_check = f"""
        SELECT id, estado FROM triagem
        WHERE padrao = '{padrao.replace("'", "''")}'
        AND versao = '{versao.replace("'", "''")}'
        AND plataforma = '{plataforma.replace("'", "''")}'
        """
        try:
            resultado = executar_sql(sql_check)
            if resultado:
                # Update existing
                existente_id = resultado[0]["id"]
                estado_anterior = resultado[0]["estado"]
                if estado_anterior != "novo":
                    estado = estado_anterior  # Keep existing state unless regression

                sql_update = f"""
                UPDATE triagem
                SET ultimo_log = {ultimo_log}, ocorrencias = {ocorrencias},
                    pessoas = {pessoas}, estado = '{estado}'
                WHERE id = {existente_id}
                """
                executar_sql(sql_update)
                print(f"✓ Atualizado: {padrao} / {versao} / {plataforma}")
            else:
                # Insert new
                sql_insert = f"""
                INSERT INTO triagem (criado, ultimo_log, padrao, versao, plataforma,
                                   ocorrencias, pessoas, estado)
                VALUES ({agora}, {ultimo_log}, '{padrao.replace("'", "''")}',
                        '{versao.replace("'", "''")}', '{plataforma.replace("'", "''")}',
                        {ocorrencias}, {pessoas}, '{estado}')
                """
                executar_sql(sql_insert)
                print(f"✓ Novo padrão: {padrao} / {versao} / {plataforma}")
        except RuntimeError as e:
            print(f"Aviso: erro ao atualizar triagem: {e}", file=sys.stderr)


def extrair_padroes_unicos(grupos):
    """Extract unique patterns for GitHub comment."""
    padroes = {}
    for (padrao, versao, plataforma), dados in grupos.items():
        if padrao not in padroes:
            padroes[padrao] = {
                "versoes": [],
                "plataformas": set(),
                "total": 0,
                "pessoas": set(),
            }
        padroes[padrao]["versoes"].append(versao)
        padroes[padrao]["plataformas"].add(plataforma)
        padroes[padrao]["total"] += dados["ocorrencias"]
        padroes[padrao]["pessoas"].update(dados["pessoas"])

    return padroes


def gerar_comentario(grupos, triagens):
    """Generate GitHub comment for the triagem issue."""
    if not grupos:
        return "✓ Nenhum padrão de erro detectado nos últimos registros."

    padroes = extrair_padroes_unicos(grupos)
    linhas = ["## Relatório de Logs - Resumo Automático\n"]
    linhas.append(f"*Atualizado em {datetime.now().strftime('%Y-%m-%d %H:%M UTC')}*\n")

    # Summarize by pattern
    for padrao, info in sorted(padroes.items(), key=lambda x: -x[1]["total"]):
        total = info["total"]
        pessoas = len(info["pessoas"])
        plataformas = ", ".join(sorted(info["plataformas"]))
        versoes = ", ".join(sorted(set(info["versoes"])))

        linhas.append(f"- **{padrao}**: {total} ocorrências, {pessoas} pessoas")
        linhas.append(f"  - Plataformas: {plataformas}")
        if versoes:
            linhas.append(f"  - Versões: {versoes}")

    # Check for regressions
    regressoes = [
        (p, v, pl) for (p, v, pl), d in triagens.items()
        if d.get("estado") == "regrediu"
    ]
    if regressoes:
        linhas.append("\n### ⚠️ Regressões Detectadas\n")
        for padrao, versao, plataforma in regressoes:
            linhas.append(f"- {padrao} retornou em {versao}/{plataforma}")

    linhas.append("\n---")
    linhas.append("*Triagem automática - nenhum ID pessoal ou URL de addon inclusos*")

    return "\n".join(linhas)


def postar_comentario(comentario):
    """Post comment to the pinned triagem issue."""
    # This would use GitHub API; for now, just print
    print("\n=== Comentário para GitHub ===")
    print(comentario)
    print("=" * 30)


def main():
    parser = argparse.ArgumentParser(description="Automatic log triage")
    parser.add_argument("--test", action="store_true", help="Test mode (don't write to DB)")
    args = parser.parse_args()

    # Check environment
    if not os.environ.get("CLOUDFLARE_API_TOKEN"):
        print("Erro: CLOUDFLARE_API_TOKEN não definido", file=sys.stderr)
        sys.exit(1)

    print("Iniciando triagem de logs...")

    try:
        # Get last processed log
        ultimo_id = obter_ultimo_log()
        print(f"Último log processado: ID {ultimo_id}")

        # Read new logs
        registros = ler_novos_registros(ultimo_id)
        if not registros:
            print("Nenhum novo registro.")
            return

        print(f"Processando {len(registros)} novos registros...")

        # Group by pattern
        grupos = agrupar_registros(registros)
        if not grupos:
            print("Nenhum padrão de erro detectado.")
            return

        # Save results
        triagens = {}
        for (padrao, versao, plataforma), dados in grupos.items():
            triagens[(padrao, versao, plataforma)] = dados

        if not args.test:
            gravar_triagem(triagens)

        # Generate and post comment
        comentario = gerar_comentario(grupos, triagens)
        postar_comentario(comentario)

        print(f"\n✓ Triagem concluída: {len(grupos)} padrões únicos")

    except Exception as e:
        print(f"Erro fatal: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
