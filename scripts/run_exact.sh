#!/usr/bin/env bash
#
# ./scripts/run_exact.sh [--time-limit SEG] [--gap G] [...]
#   1. ./build/dump_instancia
#   2. python exact/main.py
#   3. ./build/eval_real <alocacao>

set -euo pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$RAIZ"

DUMP_DIR="data/dump"
SAIDA="results/alocacao_mip.csv"

erro() { printf '\nerro: %s\n' "$1" >&2; exit 1; }
etapa() { printf '\n=== %s ===\n' "$1"; }

[[ -x ./build/dump_instancia ]] || erro "./build/dump_instancia nao existe. Compile com:
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel"

[[ -x ./build/eval_real ]] || erro "./build/eval_real nao existe. Compile com:
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel"

[[ -f exact/main.py ]] || erro "exact/main.py nao encontrado"

if [[ -z "${PYTHON:-}" && -x .venv/bin/python ]]; then
    PYTHON=".venv/bin/python"
fi
PYTHON="${PYTHON:-python3}"
command -v "$PYTHON" >/dev/null || erro "$PYTHON nao encontrado (defina PYTHON=...)"

"$PYTHON" -c 'import gurobipy' 2>/dev/null || erro "gurobipy nao instalado em $PYTHON. Crie o ambiente com:
  python3 -m venv .venv && .venv/bin/pip install -r exact/requirements.txt"

if ! grep -qE '^(def|class|import|from|if __name__)' exact/main.py; then erro "modelo nao implementado"
fi

etapa "1/3  dump da instancia"
./build/dump_instancia "$DUMP_DIR"

etapa "2/3  modelo exato (Gurobi)"
"$PYTHON" exact/main.py --dump "$DUMP_DIR" --out "$SAIDA" "$@"

[[ -f "$SAIDA" ]] || erro "exact/main.py terminou sem gravar $SAIDA"

etapa "3/3  avaliacao C++"
./build/eval_real "$SAIDA"

printf '\nAlocacao: %s\n' "$SAIDA"
