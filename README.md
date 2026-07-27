## Requisitos

- CMake 3.16 ou superior
- Compilador com suporte a C++17 (GCC ou Clang)
- Python 3.10 ou superior
- Licença acadêmica do Gurobi em `~/gurobi.lic`

## Instalação

Na raiz do repositório:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

python3 -m venv .venv
.venv/bin/pip install -r exact/requirements.txt
```

## Execução

Todos os comandos são executados na raiz do repositório, pois os arquivos de
entrada são lidos de `data/`.

### Metaheurísticas

ILS (padrão) e VNS:

```bash
./build/pas --ils
./build/pas --vns
```

A melhor alocação vai para `results/alocacao.csv`.

### Modelo exato

```bash
./scripts/run_exact.sh --gap 0
```

etapa por etapa:

```bash
./build/dump_instancia  # exporta a instância para data/dump/
.venv/bin/python exact/main.py --gap 0  # resolve -> results/alocacao_mip.csv
./build/eval_real results/alocacao_mip.csv  # avalia a solução
```

O passo do dump só precisa ser refeito quando `data/*.csv` ou `src/parser.cpp`
mudarem.

### Opções

| Flag | Efeito |
|---|---|
| `--gap 0` | exige ótimo provado (sem isso o Gurobi para em 0,01%) |
| `--resumo ARQ.json` | grava status, custo, limitante, gap, tempo e nós |
| `--warm-start CSV` | parte de uma solução existente |
| `--sem-distancia` / `--sem-consistencia` | desliga termos do objetivo |
| `--time-limit S` / `--threads N` | controles do solver |
| `--quiet` | suprime o log do Gurobi |

## Avaliação

`eval_real` avalia qualquer alocação com os mesmos critérios do solver. Aceita
CSVs com as colunas `idx_ocorrencia` e `codigo_sala`, localizadas pelo nome no
cabeçalho:

```bash
./build/eval_real results/alocacao_mip.csv  # modelo exato
./build/eval_real results/alocacao.csv  # ILS/VNS
./build/eval_real  # data/agendamento_real.csv (padrão)
```

O número oficial é sempre o do `eval_real`, não o objetivo do Gurobi.
