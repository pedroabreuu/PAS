from __future__ import annotations
from dataclasses import dataclass
import gurobipy as gp
from gurobipy import GRB
from instancia import TIPOS, Instancia

@dataclass
class Modelo:
    m: gp.Model
    inst: Instancia
    x: dict[tuple[int, int], gp.Var]
    y: dict[tuple[int, int], gp.Var]

    def alocacao(self) -> dict[int, int]:
        # ocorrencia -> sala, a partir da solucao corrente
        out: dict[int, int] = {}
        for (o, s), var in self.x.items():
            if var.X > 0.5:
                out[o] = s
        return out


def _coef_capacidade(inst: Instancia, o: int, s: int) -> int:
    # custo de alocar a ocorrencia o na sala s
    p = inst.parametros
    demanda = inst.turmas[inst.ocorrencias[o].idx_turma].demanda
    capacidade = inst.salas[s].capacidade

    if demanda <= 0 or capacidade <= 0:
        return 0
    if demanda > capacidade:
        return p.custo_suave(
            p.peso_capacidade_excesso * (demanda - capacidade),
            p.normalizador_capacidade_excesso,
        )
    return p.custo_suave(
        p.peso_capacidade_sobra * (capacidade - demanda),
        p.normalizador_capacidade_sobra,
    )


def _coef_distancia(inst: Instancia, s1: int, s2: int) -> int:
    # custo por par de salas distintas usadas pela mesma turma
    p = inst.parametros
    return p.custo_suave(p.peso_distancia * inst.distancia(s1, s2), p.normalizador_distancia)


def _coef_consistencia(inst: Instancia) -> int:
    p = inst.parametros
    coef = p.custo_suave(p.peso_consistencia_turma_tipo, p.normalizador_consistencia)
    for k in range(2, len(inst.salas) + 1):
        exato = p.custo_suave(p.peso_consistencia_turma_tipo * k, p.normalizador_consistencia)
        if exato != coef * k:
            raise ValueError(
                "custo de consistencia deixou de ser linear com estes parametros "
                f"(k={k}: exato={exato}, linear={coef * k}). "
                "Modele o termo por partes antes de comparar com o arbitro."
            )
    return coef


def construir(inst: Instancia, *, com_consistencia: bool = True, com_distancia: bool = True,) -> Modelo:
    m = gp.Model("alocacao_salas")
    x = {
        (o, s): m.addVar(vtype=GRB.BINARY, obj=_coef_capacidade(inst, o, s), name=f"x[{o},{s}]")
        for o in inst.dominio
        for s in inst.dominio[o]
    }

    # toda ocorrencia recebe exatamente uma sala
    for o, salas in inst.dominio.items():
        m.addConstr(gp.quicksum(x[o, s] for s in salas) == 1, name=f"atribuicao[{o}]")

    # uma sala nao pode receber duas ocorrencias no mesmo slot
    for slot, ocs in inst.ocorrencias_do_slot.items():
        por_sala: dict[int, list[int]] = {}
        for o in ocs:
            for s in inst.dominio[o]:
                por_sala.setdefault(s, []).append(o)
        for s, lista in por_sala.items():
            if len(lista) > 1:
                m.addConstr(
                    gp.quicksum(x[o, s] for o in lista) <= 1, name=f"conflito[{slot},{s}]"
                )

    # y[t, s] turma t usa a sala s em alguma ocorrencia
    # so para turmas com >=2 ocorrencias, as demais tem uma unica sala, logo zero pares de distancia e zero salas excedentes.
    y: dict[tuple[int, int], gp.Var] = {}
    if com_consistencia or com_distancia:
        for t in inst.turmas_acopladas():
            ocs = inst.ocorrencias_da_turma[t]
            for s in inst.salas_possiveis_da_turma(t):
                # y so aparece no objetivo com coeficiente positivo, entao minimizacao ja a empurra para o minimo, basta limitar por baixo
                y[t, s] = m.addVar(vtype=GRB.BINARY, name=f"y[{t},{s}]")
                for o in ocs:
                    if s in inst.dominio[o]:
                        m.addConstr(y[t, s] >= x[o, s], name=f"liga_y[{t},{s},{o}]")

    if com_consistencia:
        coef = _coef_consistencia(inst)
        for t in inst.turmas_acopladas():
            salas_t = inst.salas_possiveis_da_turma(t)
            for tipo in TIPOS:
                do_tipo = [s for s in salas_t if inst.salas[s].tipo == tipo]
                if len(do_tipo) < 2:
                    continue  # no maximo uma sala desse tipo
                excedente = m.addVar(lb=0.0, obj=coef, name=f"exc[{t},{tipo}]")
                m.addConstr(
                    excedente >= gp.quicksum(y[t, s] for s in do_tipo) - 1,
                    name=f"consistencia[{t},{tipo}]",
                )

    if com_distancia:
        for t in inst.turmas_acopladas():
            salas_t = inst.salas_possiveis_da_turma(t)
            for i, s1 in enumerate(salas_t):
                for s2 in salas_t[i + 1 :]:
                    coef_d = _coef_distancia(inst, s1, s2)
                    if coef_d == 0:
                        continue
                    z = m.addVar(lb=0.0, ub=1.0, obj=coef_d, name=f"z[{t},{s1},{s2}]")
                    m.addConstr(z >= y[t, s1] + y[t, s2] - 1, name=f"par[{t},{s1},{s2}]")

    m.ModelSense = GRB.MINIMIZE
    m.update()
    return Modelo(m=m, inst=inst, x=x, y=y)


def aplicar_warm_start(modelo: Modelo, alocacao: dict[int, int]) -> int:
    aplicados = 0
    for (o, s), var in modelo.x.items():
        esperada = alocacao.get(o)
        if esperada is None:
            continue
        var.Start = 1.0 if s == esperada else 0.0
        if s == esperada:
            aplicados += 1
    modelo.m.update()
    return aplicados
