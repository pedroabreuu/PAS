from __future__ import annotations
import csv
import json
from pathlib import Path
from instancia import Instancia

def gravar_alocacao(caminho: str | Path, inst: Instancia, alocacao: dict[int, int]) -> None:
    p = Path(caminho)
    p.parent.mkdir(parents=True, exist_ok=True)

    faltando = [o.idx for o in inst.ocorrencias if o.idx not in alocacao]
    if faltando:
        raise ValueError(f"{len(faltando)} ocorrencia(s) sem sala na solucao")

    with p.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["idx_ocorrencia", "codigo_sala"])
        for o in sorted(alocacao):
            w.writerow([o, inst.salas[alocacao[o]].codigo])

def gravar_resumo(caminho: str | Path, resumo: dict) -> None:
    p = Path(caminho)
    p.parent.mkdir(parents=True, exist_ok=True)
    with p.open("w", encoding="utf-8") as f:
        json.dump(resumo, f, indent=2, ensure_ascii=False)
        f.write("\n")

def ler_alocacao_csv(caminho: str | Path, inst: Instancia) -> dict[int, int]:
    cod_para_idx = {s.codigo: s.idx for s in inst.salas}
    out: dict[int, int] = {}

    with Path(caminho).open(newline="", encoding="utf-8-sig") as f:
        for linha in csv.DictReader(f):
            if "idx_ocorrencia" not in linha or "codigo_sala" not in linha:
                raise ValueError(f"{caminho} nao tem as colunas idx_ocorrencia e codigo_sala")
            sala = cod_para_idx.get((linha["codigo_sala"] or "").strip())
            if sala is not None:
                out[int(linha["idx_ocorrencia"])] = sala
    return out
