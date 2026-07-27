from __future__ import annotations
import csv
from dataclasses import dataclass, field
from pathlib import Path

TIPOS = ("Sala", "LabInformatica", "LabEspecifico", "Outro")

@dataclass(frozen=True)
class Sala:
    idx: int
    codigo: str
    nome_original: str
    unidade: str
    andar: int
    capacidade: int
    tipo: str
    acessibilidade: bool
    disponivel: bool

@dataclass(frozen=True)
class Turma:
    idx: int
    id_turma: int
    codigo_uc: str
    disciplina: str
    subgrupo: str
    termo: int
    vagas: int
    inscritos: int
    demanda: int
    docente: str
    departamento: str
    acessibilidade: bool

@dataclass(frozen=True)
class Ocorrencia:
    idx: int
    idx_turma: int
    dia: str
    inicio: str
    fim: str
    slot: int
    tipo_requerido: str

@dataclass(frozen=True)
class Parametros:
    peso_consistencia_turma_tipo: int
    peso_distancia: int
    peso_capacidade_sobra: int
    peso_capacidade_excesso: int
    penalidade_dist_desconhecida: int
    escala_normalizacao: int
    normalizador_consistencia: int
    normalizador_distancia: int
    normalizador_capacidade_excesso: int
    normalizador_capacidade_sobra: int
    normalizar_custos_suaves: bool
    normalizador_por_range: bool
    duracao_padrao_min: int

    def custo_suave(self, bruto: int, normalizador: int) -> int:
        if not self.normalizar_custos_suaves or normalizador <= 0:
            return int(bruto)
        return (bruto * self.escala_normalizacao + normalizador // 2) // normalizador


@dataclass
class Instancia:
    salas: list[Sala]
    turmas: list[Turma]
    ocorrencias: list[Ocorrencia]
    parametros: Parametros
    dominio: dict[int, list[int]]
    _dist: dict[tuple[int, int], int] = field(repr=False)
    ocorrencias_da_turma: dict[int, list[int]] = field(default_factory=dict, repr=False)
    ocorrencias_do_slot: dict[int, list[int]] = field(default_factory=dict, repr=False)

    @property
    def n_slots(self) -> int:
        return len(self.ocorrencias_do_slot)

    def distancia(self, s1: int, s2: int) -> int:
        if s1 == s2:
            return 0
        chave = (s1, s2) if s1 < s2 else (s2, s1)
        return self._dist[chave]

    def turmas_acopladas(self) -> list[int]:
        return [t for t, ocs in self.ocorrencias_da_turma.items() if len(ocs) >= 2]

    def salas_possiveis_da_turma(self, t: int) -> list[int]:
        salas: set[int] = set()
        for o in self.ocorrencias_da_turma[t]:
            salas.update(self.dominio[o])
        return sorted(salas)

def _ler(caminho: Path) -> list[dict[str, str]]:
    if not caminho.exists():
        raise FileNotFoundError(
            f"{caminho} nao encontrado. Gere o dump com: ./build/dump_instancia"
        )
    with caminho.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))

def carregar(dump_dir: str | Path = "data/dump") -> Instancia:
    d = Path(dump_dir)

    salas = [
        Sala(
            idx=int(r["idx"]),
            codigo=r["codigo"],
            nome_original=r["nome_original"],
            unidade=r["unidade"],
            andar=int(r["andar"]),
            capacidade=int(r["capacidade"]),
            tipo=r["tipo"],
            acessibilidade=r["acessibilidade"] == "1",
            disponivel=r["disponivel"] == "1",
        )
        for r in _ler(d / "salas.csv")
    ]

    turmas = [
        Turma(
            idx=int(r["idx"]),
            id_turma=int(r["id_turma"]),
            codigo_uc=r["codigo_uc"],
            disciplina=r["disciplina"],
            subgrupo=r["subgrupo"],
            termo=int(r["termo"]),
            vagas=int(r["vagas"]),
            inscritos=int(r["inscritos"]),
            demanda=int(r["demanda"]),
            docente=r["docente"],
            departamento=r["departamento"],
            acessibilidade=r["acessibilidade"] == "1",
        )
        for r in _ler(d / "turmas.csv")
    ]

    ocorrencias = [
        Ocorrencia(
            idx=int(r["idx"]),
            idx_turma=int(r["idx_turma"]),
            dia=r["dia"],
            inicio=r["inicio"],
            fim=r["fim"],
            slot=int(r["slot"]),
            tipo_requerido=r["tipo_requerido"],
        )
        for r in _ler(d / "ocorrencias.csv")
    ]

    p = {r["chave"]: int(r["valor"]) for r in _ler(d / "parametros.csv")}
    parametros = Parametros(
        peso_consistencia_turma_tipo=p["pesoConsistenciaTurmaTipo"],
        peso_distancia=p["pesoDistancia"],
        peso_capacidade_sobra=p["pesoCapacidadeSobra"],
        peso_capacidade_excesso=p["pesoCapacidadeExcesso"],
        penalidade_dist_desconhecida=p["penalidadeDistDesconhecida"],
        escala_normalizacao=p["escalaNormalizacao"],
        normalizador_consistencia=p["normalizadorConsistencia"],
        normalizador_distancia=p["normalizadorDistancia"],
        normalizador_capacidade_excesso=p["normalizadorCapacidadeExcesso"],
        normalizador_capacidade_sobra=p["normalizadorCapacidadeSobra"],
        normalizar_custos_suaves=bool(p["normalizarCustosSuaves"]),
        normalizador_por_range=bool(p["normalizadorPorRange"]),
        duracao_padrao_min=p["duracaoPadraoMin"],
    )

    dominio: dict[int, list[int]] = {o.idx: [] for o in ocorrencias}
    for r in _ler(d / "dominios.csv"):
        dominio[int(r["idx_ocorrencia"])].append(int(r["idx_sala"]))

    dist: dict[tuple[int, int], int] = {}
    for r in _ler(d / "distancias.csv"):
        i, j = int(r["idx_sala_i"]), int(r["idx_sala_j"])
        dist[(i, j)] = (
            int(r["distancia"])
            if r["conhecida"] == "1"
            else parametros.penalidade_dist_desconhecida
        )

    inst = Instancia(
        salas=salas,
        turmas=turmas,
        ocorrencias=ocorrencias,
        parametros=parametros,
        dominio=dominio,
        _dist=dist,
    )

    for o in ocorrencias:
        inst.ocorrencias_da_turma.setdefault(o.idx_turma, []).append(o.idx)
        inst.ocorrencias_do_slot.setdefault(o.slot, []).append(o.idx)

    _validar(inst)
    return inst

def _validar(inst: Instancia) -> None:
    n_salas = len(inst.salas)

    sem_dominio = [o.idx for o in inst.ocorrencias if not inst.dominio[o.idx]]
    if sem_dominio:
        raise ValueError(
            f"{len(sem_dominio)} ocorrencia(s) sem sala permitida"
        )

    for o in inst.ocorrencias:
        if not 0 <= o.idx_turma < len(inst.turmas):
            raise ValueError(f"ocorrencia {o.idx} referencia turma inexistente {o.idx_turma}")
        for s in inst.dominio[o.idx]:
            if not 0 <= s < n_salas:
                raise ValueError(f"ocorrencia {o.idx} referencia sala inexistente {s}")

    esperados = n_salas * (n_salas - 1) // 2
    if len(inst._dist) != esperados:
        raise ValueError( f"distancias.csv tem {len(inst._dist)} pares, esperado {esperados} " f"para {n_salas} salas")
