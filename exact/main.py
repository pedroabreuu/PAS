from __future__ import annotations
import argparse
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import gurobipy as gp
from gurobipy import GRB
import exportar
import instancia as inst_mod
import modelo as modelo_mod

STATUS = {
    GRB.OPTIMAL: "OPTIMAL",
    GRB.INFEASIBLE: "INFEASIBLE",
    GRB.INF_OR_UNBD: "INF_OR_UNBD",
    GRB.UNBOUNDED: "UNBOUNDED",
    GRB.TIME_LIMIT: "TIME_LIMIT",
    GRB.INTERRUPTED: "INTERRUPTED",
    GRB.SUBOPTIMAL: "SUBOPTIMAL",
}

def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Modelo exato (MILP) da alocacao de salas")
    p.add_argument("--dump", default="data/dump", help="diretorio do dump da instancia")
    p.add_argument("--out", default="results/alocacao_mip.csv", help="csv de saida")
    p.add_argument("--resumo", default=None, help="json com o resumo da execucao")
    p.add_argument("--time-limit", type=float, default=None, help="limite de tempo (s)")
    p.add_argument("--gap", type=float, default=None, help="MIPGap relativo (ex.: 0.01)")
    p.add_argument("--threads", type=int, default=None, help="threads do Gurobi")
    p.add_argument("--warm-start", default=None, help="alocacao inicial (ILS/VNS)")
    p.add_argument("--sem-distancia", action="store_true", help="desliga o termo de distancia")
    p.add_argument("--sem-consistencia", action="store_true", help="desliga o termo de consistencia")
    p.add_argument("--log", default=None, help="arquivo de log do Gurobi")
    p.add_argument("--quiet", action="store_true", help="sem log no terminal")
    return p.parse_args(argv)

def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    inst = inst_mod.carregar(args.dump)
    print(
        f"Instancia: {len(inst.salas)} salas, {len(inst.turmas)} turmas, "
        f"{len(inst.ocorrencias)} ocorrencias, {inst.n_slots} slots, "
        f"{sum(len(v) for v in inst.dominio.values())} pares (ocorrencia, sala)"
    )

    termos = ["capacidade"]
    if not args.sem_consistencia:
        termos.append("consistencia")
    if not args.sem_distancia:
        termos.append("distancia")
    print(f"Termos do objetivo: {', '.join(termos)}")

    t0 = time.perf_counter()
    mod = modelo_mod.construir(
        inst,
        com_consistencia=not args.sem_consistencia,
        com_distancia=not args.sem_distancia,
    )
    m = mod.m
    print(
        f"Modelo: {m.NumVars} variaveis ({m.NumBinVars} binarias), "
        f"{m.NumConstrs} restricoes  [{time.perf_counter() - t0:.1f}s]"
    )

    if args.quiet:
        m.Params.OutputFlag = 0
    if args.log:
        Path(args.log).parent.mkdir(parents=True, exist_ok=True)
        m.Params.LogFile = args.log
    if args.time_limit is not None:
        m.Params.TimeLimit = args.time_limit
    if args.gap is not None:
        m.Params.MIPGap = args.gap
    if args.threads is not None:
        m.Params.Threads = args.threads

    if args.warm_start:
        alvo = exportar.ler_alocacao_csv(args.warm_start, inst)
        n = modelo_mod.aplicar_warm_start(mod, alvo)
        print(f"Warm start: {n}/{len(inst.ocorrencias)} ocorrencias de {args.warm_start}")

    m.optimize()

    status = STATUS.get(m.Status, str(m.Status))
    if m.SolCount == 0:
        print(f"\nSem solucao. Status: {status}")
        if m.Status == GRB.INFEASIBLE:
            print(
                "Modelo infactivel: nao existe alocacao com zero inviabilidades. "
                "Rode m.computeIIS() para localizar o conflito."
            )
        return 1

    alocacao = mod.alocacao()
    exportar.gravar_alocacao(args.out, inst, alocacao)

    gap = m.MIPGap if m.SolCount > 0 else float("nan")
    print(f"\nStatus: {status}")
    print(f"Custo (objetivo do modelo): {m.ObjVal:.0f}")
    print(f"Limitante inferior: {m.ObjBound:.0f}")
    print(f"Gap: {100 * gap:.2f}%")
    print(f"Tempo: {m.Runtime:.1f}s   Nos: {int(m.NodeCount)}")
    print(f"Alocacao gravada em: {args.out}")

    resumo = {
        "status": status,
        "custo_modelo": m.ObjVal,
        "limitante_inferior": m.ObjBound,
        "gap_relativo": gap,
        "tempo_s": m.Runtime,
        "nos": int(m.NodeCount),
        "variaveis": m.NumVars,
        "binarias": m.NumBinVars,
        "restricoes": m.NumConstrs,
        "termos": termos,
        "warm_start": args.warm_start,
        "time_limit": args.time_limit,
        "gap_alvo": args.gap,
        "saida": args.out,
    }
    if args.resumo:
        exportar.gravar_resumo(args.resumo, resumo)
        print(f"Resumo gravado em: {args.resumo}")

    print("\nValide com:  ./build/eval_real " + args.out)
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except gp.GurobiError as e:
        print(f"\nErro do Gurobi: {e}", file=sys.stderr)
        raise SystemExit(1) from e
