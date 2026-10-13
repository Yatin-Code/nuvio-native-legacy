#!/bin/bash
# tools/parity-guard-aj.sh — trava AJ_* contra mudanca silenciosa (gate 0).
#
# O enum AJ_* (src/ajustes.c), as CHAVE[] posicionais e os padroes posicionais
# (src/ajustes_ux_padrao.inc) andam juntos: inserir no meio desloca tudo
# (o defeito do #149). Este guard lista os identificadores AJ_* na base e
# compara com a arvore atual — ordem conta, nao so presenca — e faz o mesmo
# com a lista ordenada de chaves CHAVE[]. Qualquer diferenca = saida 1.
#
#   bash tools/parity-guard-aj.sh [base]   # padrao: db6ae4ee (base do lane)
set -uo pipefail
cd "$(dirname "$0")/.."

BASE="${1:-db6ae4ee}"
AJ_FILES="src/ajustes.c src/ajustes.h src/ajustes_ux_padrao.inc"
FALHOU=0

ids_de() { grep -o 'AJ_[A-Za-z0-9_]*' "$1" | awk '!v[$0]++' || true; }
chaves_de() {
  sed 's://.*::' "$1" \
    | awk '/static const char \*CHAVE\[\] = \{/{dentro=1; next} dentro && /^\};/{exit} dentro' \
    | grep -o '"[A-Za-z0-9_.~-]*"' || true
}

echo "== parity-guard-aj: base $BASE =="
for f in $AJ_FILES; do
  if ! git cat-file -e "$BASE:$f" 2>/dev/null; then
    echo "SKIP $f (ausente na base)"
    continue
  fi
  base_ids="$(git show "$BASE:$f" | ids_de /dev/stdin)"
  atual_ids="$(ids_de "$f")"
  n_base="$(echo "$base_ids" | grep -c . || true)"
  n_atual="$(echo "$atual_ids" | grep -c . || true)"
  if [ "$base_ids" = "$atual_ids" ]; then
    echo "OK   $f ($n_atual ids AJ_*, ordem igual a base)"
  else
    echo "FALHOU $f (base: $n_base ids, atual: $n_atual ids):"
    diff <(echo "$base_ids") <(echo "$atual_ids") || true
    FALHOU=1
  fi
done

base_chaves="$(git show "$BASE:src/ajustes.c" | chaves_de /dev/stdin)"
atual_chaves="$(chaves_de src/ajustes.c)"
n_chaves="$(echo "$atual_chaves" | grep -c . || true)"
if [ "$base_chaves" = "$atual_chaves" ]; then
  echo "OK   CHAVE[] em src/ajustes.c ($n_chaves chaves, ordem igual a base)"
else
  echo "FALHOU CHAVE[] em src/ajustes.c:"
  diff <(echo "$base_chaves") <(echo "$atual_chaves") || true
  FALHOU=1
fi

if [ "$FALHOU" = 0 ]; then
  echo "guard AJ_*: PASS (nada de AJ_* nem CHAVE[] mudou desde $BASE)"
else
  echo "guard AJ_*: FAIL — ver 'git diff $BASE -- src/ajustes.c src/ajustes_ux_padrao.inc'"
  exit 1
fi
