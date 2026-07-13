# Alocação de Salas
Solução em C++ para o problema de alocação de salas, com as metaheurísticas
**Iterated Local Search (ILS)** e **Variable Neighborhood Search (VNS)**.

## Requisitos
- CMake 3.16 ou superior
- Compilador com suporte a C++17 (GCC ou Clang)

## Compilação
Na raiz do repositório, execute:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Execução
Os comandos devem ser executados na raiz do repositório, pois os arquivos de
entrada são lidos do diretório `data/`.

Para executar com ILS, método padrão:
```bash
./build/pas
```
Ou, explicitamente:
```bash
./build/pas --ils
```

Para executar com VNS:
```bash
./build/pas --vns
```

A melhor alocação encontrada é gravada em `results/alocacao.csv`. Um resumo da
instância, os custos, as inviabilidades e o tempo de execução são exibidos no
terminal.

## Avaliação do agendamento real
Para avaliar o arquivo `data/agendamento_real.csv` com os mesmos critérios do
solver:
```bash
./build/eval_real
```

## Dados
Os arquivos CSV usados pelo programa estão em `data/`. As sementes utilizadas
nas execuções estão em `Sementes_Taillard.txt`.
