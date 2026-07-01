#include "Solver.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Utils.h"

constexpr int K_MAX_VNS = 3;

namespace {

struct SalaCandidata {
    int sala = -1;
    int score = std::numeric_limits<int>::max();
};

struct SolverCache {
    std::vector<std::vector<int>> salasCompativeisPorOc;
    std::vector<int> qtdSalasCompativeis;
    std::vector<std::vector<int>> ocorrenciasPorSlot;
    std::vector<int> turmaDaOcorrencia;
    std::vector<int> slotDaOcorrencia;
    std::vector<int> demandaTurma;
    std::vector<TipoSala> tipoOcorrencia;
};

struct OccEval {
    int inviabilidades = 0;
    int custo = 0;
};

// variacao (inviabilidades, custo) de um movimento candidato
struct Delta {
    int dInv = 0;
    int dCusto = 0;
};

int tipoIdx(TipoSala t) {
    switch (t) {
        case TipoSala::Sala: return 0;
        case TipoSala::LabInformatica: return 1;
        case TipoSala::LabEspecifico: return 2;
        default: return 3;
    }
}
constexpr int NUM_TIPOS = 4;

struct EvalState {
    int custoTotal = 0;
    int inviabilidadesTotais = 0;

    int nSlots = 0;
    int nSalas = 0;
    int nTurmas = 0;

    std::vector<int> salaUsadaTurma; // [nTurmas][nSalas]
    std::vector<int> salaUsadaTurmaTipo; // [nTurmas][NUM_TIPOS][nSalas]
    std::vector<int> custoConsistenciaTurma; // [nTurmas]
    std::vector<int> custoDistanciaTurma; // [nTurmas]

    int& turmaSala(int t, int s) { return salaUsadaTurma[t * nSalas + s]; }
    int& turmaTipoSala(int t, int ti, int s) {
        return salaUsadaTurmaTipo[(t * NUM_TIPOS + ti) * nSalas + s];
    }
};

int demandaTurma(const Turma& t) {
    if (t.inscritos > 0) return t.inscritos;
    if (t.vagas > 0) return t.vagas;
    return 0;
}

bool salaNoDomnio(int idxOc, int idxSala, const Instancia& inst) {
    const auto& perm = inst.ocorrencias[idxOc].salasPermitidas;
    if (perm.empty()) return false;
    return std::binary_search(perm.begin(), perm.end(), idxSala);
}

bool tipoSalaPermitido(int idxOc, int idxSala, const Instancia& inst) {
    if (idxSala < 0 || idxSala >= static_cast<int>(inst.salas.size())) return false;
    const auto& tipos = inst.ocorrencias[idxOc].tiposPermitidos;
    if (tipos.empty()) return false;
    return std::find(tipos.begin(), tipos.end(), inst.salas[idxSala].tipo) != tipos.end();
}

SolverCache construirCache(const Instancia& inst, const Solucao& base) {
    SolverCache cache;
    const int nOcc = static_cast<int>(inst.ocorrencias.size());

    cache.salasCompativeisPorOc.resize(nOcc);
    cache.qtdSalasCompativeis.resize(nOcc, 0);
    cache.turmaDaOcorrencia.resize(nOcc, -1);
    cache.slotDaOcorrencia = base.slotDaOcorrencia;
    cache.demandaTurma.resize(inst.turmas.size(), 0);
    cache.tipoOcorrencia.resize(nOcc);

    for (std::size_t t = 0; t < inst.turmas.size(); ++t) {
        cache.demandaTurma[t] = demandaTurma(inst.turmas[t]);
    }

    for (int oc = 0; oc < nOcc; ++oc) {
        cache.turmaDaOcorrencia[oc] = inst.ocorrencias[oc].idxTurma;
        cache.tipoOcorrencia[oc] = inst.ocorrencias[oc].tipoSalaRequerido;

        for (int sala : inst.ocorrencias[oc].salasPermitidas) {
            cache.salasCompativeisPorOc[oc].push_back(sala);
        }
        cache.qtdSalasCompativeis[oc] = static_cast<int>(cache.salasCompativeisPorOc[oc].size());
    }

    cache.ocorrenciasPorSlot.assign(base.numeroSlots(), {});
    for (int oc = 0; oc < nOcc; ++oc) {
        cache.ocorrenciasPorSlot[base.slotDaOcorrencia[oc]].push_back(oc);
    }

    return cache;
}

bool salaCompativelNoCache(int idxOc, int idxSala, const SolverCache& cache) {
    if (idxOc < 0 || idxOc >= static_cast<int>(cache.salasCompativeisPorOc.size())) {
        return false;
    }
    const auto& salas = cache.salasCompativeisPorOc[idxOc];
    return std::binary_search(salas.begin(), salas.end(), idxSala);
}

int custoSuaveNormalizado(long long bruto, int normalizador, const SolverConfig& cfg) {
    if (!cfg.normalizarCustosSuaves || normalizador <= 0) {
        return static_cast<int>(std::min<long long>(bruto, std::numeric_limits<int>::max()));
    }
    const long long normalizado =
        (bruto * cfg.escalaNormalizacao + normalizador / 2) / normalizador;
    return static_cast<int>(std::min<long long>(normalizado, std::numeric_limits<int>::max()));
}

OccEval avaliarOcorrenciaEmSala(int oc, int sala, const Instancia& inst, const SolverConfig& cfg) {
    OccEval out;

    if (sala < 0 || sala >= static_cast<int>(inst.salas.size())) {
        out.inviabilidades += 1;
        return out;
    }

    if (!salaNoDomnio(oc, sala, inst)) {
        out.inviabilidades += 1;
    }

    if (!tipoSalaPermitido(oc, sala, inst)) {
        out.inviabilidades += 1;
    }

    const auto& turma = inst.turmas[inst.ocorrencias[oc].idxTurma];

    if (turma.acessibilidade && !inst.salas[sala].acessibilidade) {
        out.inviabilidades += 1;
    }

    const auto& salaRef = inst.salas[sala];
    const int demanda = demandaTurma(turma);
    if (demanda > 0 && salaRef.capacidade > 0) {
        if (demanda > salaRef.capacidade) {
            // capacidade excedida: penalidade linear proporcional ao excesso
            out.custo += custoSuaveNormalizado(
                static_cast<long long>(cfg.pesoCapacidadeExcesso) * (demanda - salaRef.capacidade),
                cfg.normalizadorCapacidadeExcesso,
                cfg);
        } else {
            out.custo += custoSuaveNormalizado(
                static_cast<long long>(cfg.pesoCapacidadeSobra) * (salaRef.capacidade - demanda),
                cfg.normalizadorCapacidadeSobra,
                cfg);
        }
    }

    return out;
}

int custoConsistenciaTurma(const EvalState& st, int t, const SolverConfig& cfg) {
    int custo = 0;
    for (int ti = 0; ti < NUM_TIPOS; ++ti) {
        const int base = (t * NUM_TIPOS + ti) * st.nSalas;
        int distintas = 0;
        for (int s = 0; s < st.nSalas; ++s) {
            if (st.salaUsadaTurmaTipo[base + s] > 0) ++distintas;
        }
        if (distintas > 1) {
            custo += custoSuaveNormalizado(
                static_cast<long long>(cfg.pesoConsistenciaTurmaTipo) * (distintas - 1),
                cfg.normalizadorConsistencia, cfg);
        }
    }
    return custo;
}

// dispersao intra-turma
int recomputarDistanciaTurma(const EvalState& st, int t, const Instancia& inst, const SolverConfig& cfg) {
    static std::vector<int> salasUsadas;
    salasUsadas.clear();
    const int base = t * st.nSalas;
    for (int s = 0; s < st.nSalas; ++s) {
        if (st.salaUsadaTurma[base + s] > 0) salasUsadas.push_back(s);
    }

    int custo = 0;
    for (std::size_t i = 0; i < salasUsadas.size(); ++i) {
        for (std::size_t j = i + 1; j < salasUsadas.size(); ++j) {
            const int d = inst.distSalas[salasUsadas[i]][salasUsadas[j]];
            const int dist = d >= DIST_INF ? cfg.penalidadeDistDesconhecida : d;
            custo += custoSuaveNormalizado(static_cast<long long>(cfg.pesoDistancia) * dist, cfg.normalizadorDistancia, cfg);
        }
    }
    return custo;
}

EvalState construirEstado(const Solucao& sol, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache) {
    EvalState st;
    st.nSlots = sol.numeroSlots();
    st.nSalas = static_cast<int>(inst.salas.size());
    st.nTurmas = static_cast<int>(inst.turmas.size());
    const int nOcc = static_cast<int>(inst.ocorrencias.size());

    st.salaUsadaTurma.assign(static_cast<std::size_t>(st.nTurmas) * st.nSalas, 0);
    st.salaUsadaTurmaTipo.assign(static_cast<std::size_t>(st.nTurmas) * NUM_TIPOS * st.nSalas, 0);
    st.custoConsistenciaTurma.assign(st.nTurmas, 0);
    st.custoDistanciaTurma.assign(st.nTurmas, 0);

    for (int oc = 0; oc < nOcc; ++oc) {
        const int s = sol.alocacao[oc];
        const int t = cache.turmaDaOcorrencia[oc];

        const OccEval oe = avaliarOcorrenciaEmSala(oc, s, inst, cfg);
        st.inviabilidadesTotais += oe.inviabilidades;
        st.custoTotal += oe.custo;

        if (s >= 0 && s < st.nSalas) {
            st.turmaSala(t, s)++;
            st.turmaTipoSala(t, tipoIdx(inst.salas[s].tipo), s)++;
        }
    }

    for (int t = 0; t < st.nTurmas; ++t) {
        st.custoConsistenciaTurma[t] = custoConsistenciaTurma(st, t, cfg);
        st.custoTotal += st.custoConsistenciaTurma[t];
        st.custoDistanciaTurma[t] = recomputarDistanciaTurma(st, t, inst, cfg);
        st.custoTotal += st.custoDistanciaTurma[t];
    }

    return st;
}

bool melhorQuePar(int invA, int custoA, int invB, int custoB) {
    if (invA != invB) return invA < invB;
    return custoA < custoB;
}

void atualizarTurmaDepoisMudanca(int turma, EvalState& st, const Instancia& inst, const SolverConfig& cfg) {
    st.custoTotal -= st.custoConsistenciaTurma[turma];
    st.custoConsistenciaTurma[turma] = custoConsistenciaTurma(st, turma, cfg);
    st.custoTotal += st.custoConsistenciaTurma[turma];

    st.custoTotal -= st.custoDistanciaTurma[turma];
    st.custoDistanciaTurma[turma] = recomputarDistanciaTurma(st, turma, inst, cfg);
    st.custoTotal += st.custoDistanciaTurma[turma];
}

void aplicarAtribuicao(int oc, int novaSala, Solucao& sol, EvalState& st, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache) {
    const int velhaSala = sol.alocacao[oc];
    if (velhaSala == novaSala) return;

    const int t = cache.turmaDaOcorrencia[oc];
    const int nSalas = st.nSalas;
    const int tiVelha = (velhaSala >= 0 && velhaSala < nSalas) ? tipoIdx(inst.salas[velhaSala].tipo) : tipoIdx(cache.tipoOcorrencia[oc]);
    const int tiNova = (novaSala >= 0 && novaSala < nSalas) ? tipoIdx(inst.salas[novaSala].tipo) : tipoIdx(cache.tipoOcorrencia[oc]);

    const OccEval oldEval = avaliarOcorrenciaEmSala(oc, velhaSala, inst, cfg);
    const OccEval newEval = avaliarOcorrenciaEmSala(oc, novaSala, inst, cfg);

    st.inviabilidadesTotais += (newEval.inviabilidades - oldEval.inviabilidades);
    st.custoTotal += (newEval.custo - oldEval.custo);

    if (velhaSala >= 0 && velhaSala < nSalas) {
        st.turmaSala(t, velhaSala)--;
        st.turmaTipoSala(t, tiVelha, velhaSala)--;
    }

    if (novaSala >= 0 && novaSala < nSalas) {
        st.turmaSala(t, novaSala)++;
        st.turmaTipoSala(t, tiNova, novaSala)++;
    }

    if (novaSala < 0) {
        sol.remover(oc);
    } else {
        sol.atribuir(oc, novaSala);
    }

    atualizarTurmaDepoisMudanca(t, st, inst, cfg);
}

Delta avaliarDeltaRelocate(int oc, int novaSala, const Solucao& sol, EvalState& st, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache) {
    Delta d;
    const int velhaSala = sol.alocacao[oc];
    if (velhaSala == novaSala) return d;

    const int t = cache.turmaDaOcorrencia[oc];
    const int nSalas = st.nSalas;
    const int tiVelha = (velhaSala >= 0 && velhaSala < nSalas) ? tipoIdx(inst.salas[velhaSala].tipo) : tipoIdx(cache.tipoOcorrencia[oc]);
    const int tiNova = (novaSala >= 0 && novaSala < nSalas) ? tipoIdx(inst.salas[novaSala].tipo) : tipoIdx(cache.tipoOcorrencia[oc]);

    const OccEval oldEval = avaliarOcorrenciaEmSala(oc, velhaSala, inst, cfg);
    const OccEval newEval = avaliarOcorrenciaEmSala(oc, novaSala, inst, cfg);
    d.dInv += (newEval.inviabilidades - oldEval.inviabilidades);
    d.dCusto += (newEval.custo - oldEval.custo);

    const bool mexeVelha = (velhaSala >= 0 && velhaSala < nSalas);
    const bool mexeNova = (novaSala >= 0 && novaSala < nSalas);
    if (mexeVelha) { st.turmaSala(t, velhaSala)--; st.turmaTipoSala(t, tiVelha, velhaSala)--; }
    if (mexeNova)  { st.turmaSala(t, novaSala)++; st.turmaTipoSala(t, tiNova, novaSala)++; }

    const int novoDiv = custoConsistenciaTurma(st, t, cfg);
    const int novoDist = recomputarDistanciaTurma(st, t, inst, cfg);

    if (mexeVelha) { st.turmaSala(t, velhaSala)++; st.turmaTipoSala(t, tiVelha, velhaSala)++; }
    if (mexeNova)  { st.turmaSala(t, novaSala)--; st.turmaTipoSala(t, tiNova, novaSala)--; }

    d.dCusto += (novoDiv - st.custoConsistenciaTurma[t]);
    d.dCusto += (novoDist - st.custoDistanciaTurma[t]);
    return d;
}

int scoreGulosoSala(int idxOc, int idxSala, const Solucao& sol, const Instancia& inst, const SolverConfig& cfg) {
    const auto& turma = inst.turmas[inst.ocorrencias[idxOc].idxTurma];
    int score = 0;

    const OccEval restrEval = avaliarOcorrenciaEmSala(idxOc, idxSala, inst, cfg);
    score += restrEval.inviabilidades * 100000;
    score += restrEval.custo;

    // preferir sala ja usada pela turma
    int repeticoesMesmaSala = 0;
    for (int ocTurma : sol.ocorrenciasDaTurma[turma.idx]) {
        if (ocTurma == idxOc) continue;
        if (sol.alocacao[ocTurma] == idxSala) ++repeticoesMesmaSala;
    }
    score -= 80 * repeticoesMesmaSala;

    // proximidade com salas ja usadas pela turma
    int melhorDist = std::numeric_limits<int>::max();
    for (int ocTurma : sol.ocorrenciasDaTurma[turma.idx]) {
        if (ocTurma == idxOc) continue;
        const int sAtual = sol.alocacao[ocTurma];
        if (sAtual < 0) continue;
        const int d = inst.distSalas[idxSala][sAtual];
        melhorDist = std::min(melhorDist, d >= DIST_INF ? cfg.penalidadeDistDesconhecida : d);
    }
    if (melhorDist != std::numeric_limits<int>::max()) score += melhorDist;

    return score;
}

void alocarNaMelhorSalaDisponivel(int idxOc, Solucao& sol, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache, std::mt19937* gen) {
    const int slot = sol.slotDaOcorrencia[idxOc];
    std::vector<SalaCandidata> candidatos;
    for (int s : cache.salasCompativeisPorOc[idxOc]) {
        if (!sol.slotLivre(slot, s)) continue;
        candidatos.push_back({s, scoreGulosoSala(idxOc, s, sol, inst, cfg)});
    }
    if (candidatos.empty()) return;

    if (gen == nullptr) {
        const SalaCandidata* best = &candidatos.front();
        for (const auto& c : candidatos) {
            if (c.score < best->score) best = &c;
        }
        sol.atribuir(idxOc, best->sala);
        return;
    }

    std::sort(candidatos.begin(), candidatos.end(), [](const SalaCandidata& a, const SalaCandidata& b) { return a.score < b.score; });
    const int rcl = std::min<int>(3, static_cast<int>(candidatos.size()));
    std::uniform_int_distribution<int> pick(0, rcl - 1);
    sol.atribuir(idxOc, candidatos[pick(*gen)].sala);
}

int contarOcorrenciasAlocadas(const Solucao& sol) {
    int n = 0;
    for (int s : sol.alocacao) {
        if (s >= 0) ++n;
    }
    return n;
}

bool tentarRelocate(Solucao& sol, EvalState& st, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache) {
    bool melhorou = false;
    const int nOcc = static_cast<int>(inst.ocorrencias.size());

    for (int oc = 0; oc < nOcc; ++oc) {
        const int salaAtual = sol.alocacao[oc];
        const int slot = cache.slotDaOcorrencia[oc];

        int bestSala = salaAtual;
        Delta bestDelta; // {0, 0} so movimentos que melhoram superam

        for (int s : cache.salasCompativeisPorOc[oc]) {
            if (s == salaAtual) continue;
            if (!sol.slotLivre(slot, s)) continue;

            const Delta d = avaliarDeltaRelocate(oc, s, sol, st, inst, cfg, cache);
            if (melhorQuePar(d.dInv, d.dCusto, bestDelta.dInv, bestDelta.dCusto)) {
                bestDelta = d;
                bestSala = s;
            }
        }

        if (bestSala != salaAtual) {
            aplicarAtribuicao(oc, bestSala, sol, st, inst, cfg, cache);
            sol.inviabilidades = st.inviabilidadesTotais;
            sol.custo = st.custoTotal;
            melhorou = true;
        }
    }
    return melhorou;
}

bool tentarPrimeiroSwapMesmoSlot(Solucao& sol, EvalState& st, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache) {
    for (const auto& lista : cache.ocorrenciasPorSlot) {
        for (std::size_t i = 0; i < lista.size(); ++i) {
            for (std::size_t j = i + 1; j < lista.size(); ++j) {
                const int a = lista[i];
                const int b = lista[j];
                const int sa = sol.alocacao[a];
                const int sb = sol.alocacao[b];
                if (sa < 0 || sb < 0 || sa == sb) continue;
                if (!salaCompativelNoCache(a, sb, cache)) continue;
                if (!salaCompativelNoCache(b, sa, cache)) continue;

                const int oldInv = st.inviabilidadesTotais;
                const int oldCost = st.custoTotal;

                aplicarAtribuicao(a, -1, sol, st, inst, cfg, cache);
                aplicarAtribuicao(b, -1, sol, st, inst, cfg, cache);
                aplicarAtribuicao(a, sb, sol, st, inst, cfg, cache);
                aplicarAtribuicao(b, sa, sol, st, inst, cfg, cache);

                if (melhorQuePar(st.inviabilidadesTotais, st.custoTotal, oldInv, oldCost)) {
                    sol.inviabilidades = st.inviabilidadesTotais;
                    sol.custo = st.custoTotal;
                    return true;
                }

                aplicarAtribuicao(a, -1, sol, st, inst, cfg, cache);
                aplicarAtribuicao(b, -1, sol, st, inst, cfg, cache);
                aplicarAtribuicao(a, sa, sol, st, inst, cfg, cache);
                aplicarAtribuicao(b, sb, sol, st, inst, cfg, cache);
            }
        }
    }
    return false;
}

bool tentarPrimeiroMoverTurmaTipo(Solucao& sol, EvalState& st, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache) {
    for (int t = 0; t < static_cast<int>(inst.turmas.size()); ++t) {
        const auto& occsTurma = sol.ocorrenciasDaTurma[t];
        if (occsTurma.empty()) continue;

        std::vector<std::vector<int>> occsPorTipo(NUM_TIPOS);
        for (int oc : occsTurma) {
            occsPorTipo[tipoIdx(cache.tipoOcorrencia[oc])].push_back(oc);
        }

        for (const auto& occs : occsPorTipo) {
            if (occs.size() < 2) continue;

            std::vector<int> salasComuns = cache.salasCompativeisPorOc[occs.front()];
            for (std::size_t p = 1; p < occs.size(); ++p) {
                std::vector<int> inter;
                const auto& cand = cache.salasCompativeisPorOc[occs[p]];
                std::set_intersection(salasComuns.begin(), salasComuns.end(), cand.begin(), cand.end(), std::back_inserter(inter));
                salasComuns.swap(inter);
                if (salasComuns.empty()) break;
            }

            std::unordered_set<int> usadas;
            for (int oc : occs) {
                if (sol.alocacao[oc] >= 0) usadas.insert(sol.alocacao[oc]);
            }

            std::vector<int> salasCandidatas = salasComuns;
            for (int s : usadas) {
                if (std::find(salasCandidatas.begin(), salasCandidatas.end(), s) == salasCandidatas.end()) {
                    salasCandidatas.push_back(s);
                }
            }
            if (salasCandidatas.empty()) continue;

            for (int s : salasCandidatas) {
                bool valido = true;
                for (int oc : occs) {
                    if (!salaCompativelNoCache(oc, s, cache)) {
                        valido = false;
                        break;
                    }
                    const int slot = cache.slotDaOcorrencia[oc];
                    const int ocupante = sol.ocupacao[slot][s];
                    if (ocupante != -1 && ocupante != oc) {
                        valido = false;
                        break;
                    }
                }
                if (!valido) continue;

                std::vector<int> salasAntigas;
                salasAntigas.reserve(occs.size());
                bool algumaMudanca = false;
                for (int oc : occs) {
                    salasAntigas.push_back(sol.alocacao[oc]);
                    if (sol.alocacao[oc] != s) algumaMudanca = true;
                }
                if (!algumaMudanca) continue;

                const int oldInv = st.inviabilidadesTotais;
                const int oldCost = st.custoTotal;

                for (int oc : occs) aplicarAtribuicao(oc, -1, sol, st, inst, cfg, cache);
                for (int oc : occs) aplicarAtribuicao(oc, s, sol, st, inst, cfg, cache);

                if (melhorQuePar(st.inviabilidadesTotais, st.custoTotal, oldInv, oldCost)) {
                    sol.inviabilidades = st.inviabilidadesTotais;
                    sol.custo = st.custoTotal;
                    return true;
                }

                for (int oc : occs) aplicarAtribuicao(oc, -1, sol, st, inst, cfg, cache);
                for (std::size_t i = 0; i < occs.size(); ++i) {
                    aplicarAtribuicao(occs[i], salasAntigas[i], sol, st, inst, cfg, cache);
                }
            }
        }
    }
    return false;
}

void RVND(Solucao& sol, EvalState& st, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache, std::mt19937& gen) {
    auto aplicar = [&](int viz) {
        if (viz == 0) return tentarRelocate(sol, st, inst, cfg, cache);
        if (viz == 1) return tentarPrimeiroSwapMesmoSlot(sol, st, inst, cfg, cache);
        return tentarPrimeiroMoverTurmaTipo(sol, st, inst, cfg, cache);
    };

    std::vector<int> ordem{0, 1, 2};
    std::shuffle(ordem.begin(), ordem.end(), gen);

    std::size_t i = 0;
    while (i < ordem.size()) {
        if (aplicar(ordem[i])) {
            std::shuffle(ordem.begin(), ordem.end(), gen);
            i = 0;
        } else {
            ++i;
        }
    }
}

void perturbRandomRelocate(Solucao& sol, EvalState& st, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache, std::mt19937& gen) {
    const int nOcc = static_cast<int>(inst.ocorrencias.size());

    if (nOcc <= 0) return;

    std::uniform_int_distribution<int> distOc(0, nOcc - 1);
    const int oc = distOc(gen);
    const auto& cand = cache.salasCompativeisPorOc[oc];

    if (cand.empty()) return;

    const int slot = cache.slotDaOcorrencia[oc];
    const int salaAtual = sol.alocacao[oc];

    std::uniform_int_distribution<int> pick(0, static_cast<int>(cand.size()) - 1);
    const int inicio = pick(gen);

    for (int off = 0; off < static_cast<int>(cand.size()); ++off) {
        const int s = cand[(inicio + off) % cand.size()];
        if (s == salaAtual) continue;
        if (!sol.slotLivre(slot, s)) continue;
        aplicarAtribuicao(oc, s, sol, st, inst, cfg, cache);
        return;
    }
}

void perturbRandomSwapSlot(Solucao& sol, EvalState& st, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache, std::mt19937& gen) {
    std::vector<int> slotsValidos;
    for (int k = 0; k < static_cast<int>(cache.ocorrenciasPorSlot.size()); ++k) {
        if (cache.ocorrenciasPorSlot[k].size() >= 2) slotsValidos.push_back(k);
    }

    if (slotsValidos.empty()) return;

    std::uniform_int_distribution<int> distSlot(0, static_cast<int>(slotsValidos.size()) - 1);
    const auto& lista = cache.ocorrenciasPorSlot[slotsValidos[distSlot(gen)]];
    std::uniform_int_distribution<int> distIdx(0, static_cast<int>(lista.size()) - 1);

    int i = distIdx(gen);
    int j = distIdx(gen);

    while (j == i) j = distIdx(gen);

    const int a = lista[i], b = lista[j];
    const int sa = sol.alocacao[a], sb = sol.alocacao[b];

    if (sa < 0 || sb < 0 || sa == sb) return;
    if (!salaCompativelNoCache(a, sb, cache)) return;
    if (!salaCompativelNoCache(b, sa, cache)) return;
    aplicarAtribuicao(a, -1, sol, st, inst, cfg, cache);
    aplicarAtribuicao(b, -1, sol, st, inst, cfg, cache);
    aplicarAtribuicao(a, sb, sol, st, inst, cfg, cache);
    aplicarAtribuicao(b, sa, sol, st, inst, cfg, cache);
}

void perturbRandomMoveClassBlock(Solucao& sol, EvalState& st, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache, std::mt19937& gen) {
    const int nTurmas = static_cast<int>(inst.turmas.size());
    if (nTurmas <= 0) return;

    std::uniform_int_distribution<int> distT(0, nTurmas - 1);
    const int t = distT(gen);
    const auto& occsTurma = sol.ocorrenciasDaTurma[t];

    if (occsTurma.empty()) return;

    std::vector<std::vector<int>> occsPorTipo(NUM_TIPOS);
    for (int oc : occsTurma) {
        occsPorTipo[tipoIdx(cache.tipoOcorrencia[oc])].push_back(oc);
    }

    std::vector<int> tiposValidos;
    for (int ti = 0; ti < NUM_TIPOS; ++ti) {
        if (occsPorTipo[ti].size() >= 2) tiposValidos.push_back(ti);
    }
    if (tiposValidos.empty()) return;
    std::uniform_int_distribution<int> distTi(0, static_cast<int>(tiposValidos.size()) - 1);
    const auto& occs = occsPorTipo[tiposValidos[distTi(gen)]];

    std::vector<int> salasComuns = cache.salasCompativeisPorOc[occs.front()];
    for (std::size_t p = 1; p < occs.size(); ++p) {
        std::vector<int> inter;
        const auto& cand = cache.salasCompativeisPorOc[occs[p]];
        std::set_intersection(salasComuns.begin(), salasComuns.end(),cand.begin(), cand.end(), std::back_inserter(inter));
        salasComuns.swap(inter);
        if (salasComuns.empty()) return;
    }

    std::vector<int> salasValidas;
    for (int s : salasComuns) {
        bool ok = true;
        for (int oc : occs) {
            const int slot = cache.slotDaOcorrencia[oc];
            const int ocupante = sol.ocupacao[slot][s];
            if (ocupante != -1 && ocupante != oc) { ok = false; break; }
        }
        if (ok) salasValidas.push_back(s);
    }
    if (salasValidas.empty()) return;

    std::uniform_int_distribution<int> distS(0, static_cast<int>(salasValidas.size()) - 1);
    const int novaSala = salasValidas[distS(gen)];

    for (int oc : occs) aplicarAtribuicao(oc, -1, sol, st, inst, cfg, cache);
    for (int oc : occs) aplicarAtribuicao(oc, novaSala, sol, st, inst, cfg, cache);
}

using PerturbFunc = void(*)(Solucao&, EvalState&, const Instancia&, const SolverConfig&, const SolverCache&, std::mt19937&);

void shake(Solucao& sol, EvalState& st, const Instancia& inst, const SolverConfig& cfg, const SolverCache& cache, std::mt19937& gen, int intensidade) {
    if (intensidade <= 0) return;

    static const std::vector<PerturbFunc> perturbacoes = {
        perturbRandomRelocate,
        perturbRandomSwapSlot,
        perturbRandomMoveClassBlock
    };

    std::uniform_int_distribution<int> pick(0, static_cast<int>(perturbacoes.size()) - 1);

    for (int i = 0; i < intensidade; ++i) {
        perturbacoes[pick(gen)](sol, st, inst, cfg, cache, gen);
    }

    sol.inviabilidades = st.inviabilidadesTotais;
    sol.custo = st.custoTotal;
}

// construcao gulosa gen != nullptr ativa randomizacao GRASP da escolha de sala
Solucao construirGulosoImpl(const Instancia& inst, const SolverConfig& cfg, std::mt19937* gen, const SolverCache* cacheExterno = nullptr) {
    Solucao sol(inst);
    SolverCache cacheLocal;
    if (cacheExterno == nullptr) cacheLocal = construirCache(inst, sol);
    const SolverCache& cache = (cacheExterno != nullptr) ? *cacheExterno : cacheLocal;

    std::vector<int> ordem(inst.ocorrencias.size());
    std::iota(ordem.begin(), ordem.end(), 0);

    std::stable_sort(ordem.begin(), ordem.end(), [&](int a, int b) {
        const auto ta = inst.ocorrencias[a].tipoSalaRequerido;
        const auto tb = inst.ocorrencias[b].tipoSalaRequerido;

        auto rankTipo = [](TipoSala t) {
            if (t == TipoSala::LabEspecifico) return 0;
            if (t == TipoSala::LabInformatica) return 1;
            return 2;
        };

        const int ra = rankTipo(ta);
        const int rb = rankTipo(tb);
        if (ra != rb) return ra < rb;

        if (cache.qtdSalasCompativeis[a] != cache.qtdSalasCompativeis[b]) {
            return cache.qtdSalasCompativeis[a] < cache.qtdSalasCompativeis[b];
        }

        const int da = cache.demandaTurma[cache.turmaDaOcorrencia[a]];
        const int db = cache.demandaTurma[cache.turmaDaOcorrencia[b]];
        if (da != db) return da > db;

        if (sol.slotDaOcorrencia[a] != sol.slotDaOcorrencia[b]) {
            return sol.slotDaOcorrencia[a] < sol.slotDaOcorrencia[b];
        }

        return a < b;
    });

    for (int oc : ordem) {
        alocarNaMelhorSalaDisponivel(oc, sol, inst, cfg, cache, gen);
    }

    const EvalState st = construirEstado(sol, inst, cfg, cache);
    sol.inviabilidades = st.inviabilidadesTotais;
    sol.custo = st.custoTotal;
    return sol;
}

} // namespace

void avaliarSolucao(Solucao& sol, const Instancia& inst, const SolverConfig& cfg) {
    const SolverCache cache = construirCache(inst, sol);
    const EvalState st = construirEstado(sol, inst, cfg, cache);
    sol.inviabilidades = st.inviabilidadesTotais;
    sol.custo = st.custoTotal;
}

RelatorioCusto computarRelatorio(const Solucao& sol, const Instancia& inst, const SolverConfig& cfg) {
    RelatorioCusto r;
    const SolverCache cache = construirCache(inst, sol);
    const EvalState st = construirEstado(sol, inst, cfg, cache);

    r.custoTotal = st.custoTotal;
    r.inviabilidadesTotais = st.inviabilidadesTotais;

    for (int t = 0; t < st.nTurmas; ++t) {
        r.custoConsistencia += st.custoConsistenciaTurma[t];
        r.custoDistancia += st.custoDistanciaTurma[t];
    }

    const int nSlots = sol.numeroSlots();
    const int nSalas = static_cast<int>(inst.salas.size());
    const int nTurmas = static_cast<int>(inst.turmas.size());
    const int nOcc = static_cast<int>(inst.ocorrencias.size());

    std::vector<std::vector<int>> slotRoom(nSlots, std::vector<int>(nSalas, 0));
    for (int oc = 0; oc < nOcc; ++oc) {
        const int s = sol.alocacao[oc];
        if (s < 0 || s >= nSalas) {
            ++r.ocorrenciasNaoAlocadas;
            continue;
        }
        slotRoom[cache.slotDaOcorrencia[oc]][s]++;
        if (!salaNoDomnio(oc, s, inst)) ++r.foraDominio;
        if (!tipoSalaPermitido(oc, s, inst)) ++r.tipoIncompativel;
        if (inst.turmas[inst.ocorrencias[oc].idxTurma].acessibilidade && !inst.salas[s].acessibilidade) {
            ++r.acessibilidadeViolada;
        }

        const auto& turma = inst.turmas[inst.ocorrencias[oc].idxTurma];
        const int demanda = demandaTurma(turma);
        const int cap = inst.salas[s].capacidade;
        if (demanda > 0 && cap > 0) {
            if (demanda > cap) {
                const int excesso = demanda - cap;
                ++r.capacidadeExcedida; // contador quantas salas estouraram
                r.somaExcessoCapacidade += excesso;
                r.custoCapacidade += custoSuaveNormalizado( static_cast<long long>(cfg.pesoCapacidadeExcesso) * excesso,
                    cfg.normalizadorCapacidadeExcesso,
                    cfg);
            } else {
                const int sobra = cap - demanda;
                r.somaSobraCapacidade += sobra;
                r.custoCapacidade += custoSuaveNormalizado(
                    static_cast<long long>(cfg.pesoCapacidadeSobra) * sobra,
                    cfg.normalizadorCapacidadeSobra,
                    cfg);
            }
        }
    }
    for (int k = 0; k < nSlots; ++k) {
        for (int s = 0; s < nSalas; ++s) {
            if (slotRoom[k][s] > 1) r.conflitos += slotRoom[k][s] - 1;
        }
    }

    for (int t = 0; t < nTurmas; ++t) {
        std::vector<int> salas;
        std::vector<TipoSala> tiposReq;
        for (int oc : sol.ocorrenciasDaTurma[t]) {
            const int s = sol.alocacao[oc];
            if (s >= 0 && std::find(salas.begin(), salas.end(), s) == salas.end()) {
                salas.push_back(s);
            }
            const TipoSala tp = inst.ocorrencias[oc].tipoSalaRequerido;
            if (std::find(tiposReq.begin(), tiposReq.end(), tp) == tiposReq.end()) {
                tiposReq.push_back(tp);
            }
        }
        // consistencia de sala so faz sentido para turmas de tipo unico
        if (!sol.ocorrenciasDaTurma[t].empty() && tiposReq.size() == 1) {
            ++r.turmasTipoUnico;
            if (salas.size() > 1) ++r.turmasTipoUnicoSalasDiferentes;
        }
        for (std::size_t i = 0; i < salas.size(); ++i) {
            for (std::size_t j = i + 1; j < salas.size(); ++j) {
                const int d = inst.distSalas[salas[i]][salas[j]];
                r.somaDistanciaIntraTurma += (d >= DIST_INF ? cfg.penalidadeDistDesconhecida : d);
            }
        }
    }

    return r;
}

Solucao construirSolucaoInicialGulosa(const Instancia& inst, const SolverConfig& cfg) {
    return construirGulosoImpl(inst, cfg, nullptr);
}

bool VNS(Solucao& corrente, EvalState& correnteState, const Instancia& inst, const SolverConfig& cfg,
                 const SolverCache& cache, std::mt19937& gen, SolverStats& stats) {

    //const int kMax = 4;
    
    int k = 1;
    bool encontrouMelhora = false;

    while(k <= K_MAX_VNS) {
        Solucao cand = corrente;
        EvalState candState = correnteState;

        shake(cand, candState, inst, cfg, cache, gen, k);
        ++stats.perturbacoes;

        RVND(cand, candState, inst, cfg, cache, gen);
        
        cand.inviabilidades = candState.inviabilidadesTotais;
        cand.custo = candState.custoTotal;

        if (melhorQuePar(cand.inviabilidades, cand.custo, corrente.inviabilidades, corrente.custo)) {
            corrente = std::move(cand);
            correnteState = std::move(candState);

            encontrouMelhora = true;

            k = 1;
        } else {
            ++k;
        }

    }

    return encontrouMelhora;
}

bool ILS(Solucao& corrente, EvalState& correnteState, const Instancia& inst, const SolverConfig& cfg,
         const SolverCache& cache, std::mt19937& gen, SolverStats& stats, int intensidade) {
    Solucao cand = corrente;
    EvalState candState = correnteState;

    shake(cand, candState, inst, cfg, cache, gen, intensidade);
    ++stats.perturbacoes;
    RVND(cand, candState, inst, cfg, cache, gen);
    cand.inviabilidades = candState.inviabilidadesTotais;
    cand.custo = candState.custoTotal;

    if (melhorQuePar(cand.inviabilidades, cand.custo, corrente.inviabilidades, corrente.custo)) {
        corrente = std::move(cand);
        correnteState = std::move(candState);
        return true;
    }

    // aceitacao de piora
    if (cand.inviabilidades == corrente.inviabilidades &&
        cand.custo > corrente.custo &&
        cfg.probAceitarPioraILS > 0.0) {
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        if (prob(gen) < cfg.probAceitarPioraILS) {
            corrente = std::move(cand);
            correnteState = std::move(candState);
        }
    }
    return false;
}

std::vector<unsigned> carregarSementesTaillard(const std::string& path) {
    std::vector<unsigned> sementes;
    std::ifstream in(path);
    if (!in) return sementes;
    unsigned long v;
    while (in >> v) sementes.push_back(static_cast<unsigned>(v));
    return sementes;
}

void definirNormalizadoresPorRange(const Instancia& inst, SolverConfig& cfg) {
    if (!cfg.normalizarCustosSuaves || !cfg.normalizadorPorRange) return;

    int maxCap = 0;
    int minCapPos = std::numeric_limits<int>::max();
    for (const auto& s : inst.salas) {
        if (!s.disponivel || s.capacidade <= 0) continue;
        maxCap = std::max(maxCap, s.capacidade);
        minCapPos = std::min(minCapPos, s.capacidade);
    }
    if (minCapPos == std::numeric_limits<int>::max()) minCapPos = 0;

    int maxDem = 0;
    int minDemPos = std::numeric_limits<int>::max();
    for (const auto& t : inst.turmas) {
        const int d = demandaTurma(t);
        if (d <= 0) continue;
        maxDem = std::max(maxDem, d);
        minDemPos = std::min(minDemPos, d);
    }
    if (minDemPos == std::numeric_limits<int>::max()) minDemPos = 0;

    const long long nExc = std::max<long long>(1, std::max(0, maxDem - minCapPos));
    const long long nSob = std::max<long long>(1, std::max(0, maxCap - minDemPos));
    cfg.normalizadorCapacidadeExcesso = static_cast<int>(std::min<long long>(nExc, std::numeric_limits<int>::max()));
    cfg.normalizadorCapacidadeSobra = static_cast<int>(std::min<long long>(nSob, std::numeric_limits<int>::max()));

    int maxDist = 0;
    for (const auto& linha : inst.distSalas) {
        for (int d : linha) {
            if (d >= DIST_INF) continue;
            maxDist = std::max(maxDist, d);
        }
    }
    maxDist = std::max(maxDist, cfg.penalidadeDistDesconhecida);
    const long long nDisp = std::max<long long>(1, maxDist);
    cfg.normalizadorDistancia = static_cast<int>(std::min<long long>(nDisp, std::numeric_limits<int>::max()));

    std::vector<int> contagem(inst.turmas.size() * NUM_TIPOS, 0);
    for (const auto& o : inst.ocorrencias) {
        if (o.idxTurma < 0) continue;
        contagem[o.idxTurma * NUM_TIPOS + tipoIdx(o.tipoSalaRequerido)]++;
    }
    int maxOcc = 0;
    for (int c : contagem) maxOcc = std::max(maxOcc, c);
    const int nSalas = static_cast<int>(inst.salas.size());
    const int maxDistintas = std::max(1, std::min(maxOcc, nSalas) - 1);
    const long long nCons = std::max<long long>(1, maxDistintas);
    cfg.normalizadorConsistencia = static_cast<int>(std::min<long long>(nCons, std::numeric_limits<int>::max()));
}

std::pair<Solucao, SolverStats> executar(const Instancia& inst, const SolverConfig& cfgEntrada) {
    SolverConfig cfg = cfgEntrada;
    definirNormalizadoresPorRange(inst, cfg);
    std::vector<unsigned> sementes = carregarSementesTaillard(cfg.arquivoSementes);
    if (cfg.sortearSementes && !sementes.empty()) {
        std::random_device rd;
        std::mt19937 mestre(rd());
        std::shuffle(sementes.begin(), sementes.end(), mestre);
    }
    if (cfg.verbose >= 1) {
        std::cout << "[norm] range Ncons=" << cfg.normalizadorConsistencia
                  << " Ndisp=" << cfg.normalizadorDistancia
                  << " Nexc=" << cfg.normalizadorCapacidadeExcesso
                  << " Nsob=" << cfg.normalizadorCapacidadeSobra << '\n';
        std::cout << "[seed] sementes Taillard carregadas: " << sementes.size()
                  << (cfg.sortearSementes ? " (sorteio aleatorio)" : " (ordem fixa)") << '\n';
        if (!sementes.empty()) {
            const int nStartsLog = std::max(1, cfg.numStarts);
            std::cout << "[seed] sementes usadas:";
            for (int s = 0; s < nStartsLog; ++s) std::cout << ' ' << sementes[s % sementes.size()];
            std::cout << '\n';
        }
    }

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    SolverStats stats;

    auto tempoDecorrido = [&]() {
        return std::chrono::duration<double>(clock::now() - start).count();
    };
    auto tempoEsgotado = [&]() {
        return cfg.tempoLimiteSegundos > 0.0 && tempoDecorrido() >= cfg.tempoLimiteSegundos;
    };

    const SolverCache cache = construirCache(inst, Solucao(inst));
    const char* tag = (cfg.metodo == Metaheuristica::ILS) ? "[ILS]" : "[VNS]";
    const int nStarts = std::max(1, cfg.numStarts);

    Solucao melhorGlobal;
    bool temGlobal = false;

    for (int s = 0; s < nStarts; ++s) {
        if (s > 0 && tempoEsgotado()) break;
        ++stats.starts;

        const unsigned semente = sementes.empty() ? static_cast<unsigned>(cfg.seed) + static_cast<unsigned>(s) * 42u : sementes[s % sementes.size()];
        std::mt19937 gen(semente);
        Solucao corrente = construirGulosoImpl(inst, cfg, s == 0 ? nullptr : &gen, &cache);
        EvalState correnteState = construirEstado(corrente, inst, cfg, cache);

        if (s == 0) {
            stats.inicialInviabilidades = correnteState.inviabilidadesTotais;
            stats.inicialCusto = correnteState.custoTotal;
        }

        RVND(corrente, correnteState, inst, cfg, cache, gen);
        corrente.inviabilidades = correnteState.inviabilidadesTotais;
        corrente.custo = correnteState.custoTotal;
        Solucao melhorStart = corrente;
        EvalState melhorStartState = correnteState;

        const bool ehILS = (cfg.metodo == Metaheuristica::ILS);
        const int itersPorNivel = cfg.ilsItersPorNivel > 0 ? cfg.ilsItersPorNivel : std::max(1, cfg.maxNoImprove / 10);
        const int shakeMax = cfg.ilsShakeMax > 0 ? cfg.ilsShakeMax : 3 * cfg.shakeStrength;
        const int reinicioPeriodo = cfg.ilsReinicioSemMelhora == 0 ? std::max(1, cfg.maxNoImprove / 5) : cfg.ilsReinicioSemMelhora;

        int semMelhora = 0;
        for (int iter = 1; iter <= cfg.maxIters && semMelhora < cfg.maxNoImprove; ++iter) {
            if (tempoEsgotado()) break;
            ++stats.iteracoes;

            int intensidade = cfg.shakeStrength;
            if (ehILS && cfg.ilsPerturbacaoAdaptativa) {
                const int nivel = semMelhora / itersPorNivel;
                intensidade = std::min(shakeMax, cfg.shakeStrength * (1 + nivel));
            }

            const bool melhorou = ehILS ? ILS(corrente, correnteState, inst, cfg, cache, gen, stats, intensidade)
                : VNS(corrente, correnteState, inst, cfg, cache, gen, stats);
            (void)melhorou;

            if (melhorQuePar(corrente.inviabilidades, corrente.custo, melhorStart.inviabilidades, melhorStart.custo)) {
                melhorStart = corrente;
                melhorStartState = correnteState;
                semMelhora = 0;
            } else {
                ++semMelhora;
            }

            if (!ehILS && reinicioPeriodo > 0 && semMelhora > 0 && (semMelhora % reinicioPeriodo == 0)) {
              corrente = melhorStart;
              correnteState = melhorStartState;
              
              shake(corrente, correnteState, inst, cfg, cache, gen, 4*K_MAX_VNS);
              RVND(corrente, correnteState, inst, cfg, cache, gen);

              corrente.inviabilidades = correnteState.inviabilidadesTotais;
              corrente.custo = correnteState.custoTotal;
            }

            if (ehILS && reinicioPeriodo > 0 && semMelhora > 0 &&
                (semMelhora % reinicioPeriodo == 0)) {
                corrente = melhorStart;
                correnteState = melhorStartState;
            }

            if (cfg.verbose >= 1) {
                std::cout << tag
                          << " start=" << s
                          << " iter=" << iter
                          << " semMelhora=" << semMelhora
                          << " currInv=" << corrente.inviabilidades
                          << " currCost=" << corrente.custo
                          << " bestInv=" << melhorStart.inviabilidades
                          << " bestCost=" << melhorStart.custo
                          << '\n';
            }
        }

        if (!temGlobal ||
            melhorQuePar(melhorStart.inviabilidades, melhorStart.custo,
                         melhorGlobal.inviabilidades, melhorGlobal.custo)) {
            melhorGlobal = std::move(melhorStart);
            temGlobal = true;
        }
    }

    stats.melhorInviabilidades = melhorGlobal.inviabilidades;
    stats.melhorCusto = melhorGlobal.custo;
    stats.ocorrenciasAlocadas = contarOcorrenciasAlocadas(melhorGlobal);
    stats.tempoDecorrido = tempoDecorrido();

    return {melhorGlobal, stats};
}

bool escreverAlocacaoCSV(const std::string& path, const Solucao& sol, const Instancia& inst, const SolverConfig& cfg) {
    try {
        std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }

        std::ofstream out(path);
        if (!out) return false;

        out << "idx_ocorrencia,id_turma,disciplina,dia,inicio,fim,tipos_permitidos,codigo_sala,nome_sala,tipo_sala_alocada,inscritos,capacidade_sala,custo_total,inviabilidades_total\n";

        auto tipoStr = [](TipoSala tipo) {
            switch (tipo) {
                case TipoSala::Sala: return "Sala";
                case TipoSala::LabInformatica: return "LabInformatica";
                case TipoSala::LabEspecifico: return "LabEspecifico";
                case TipoSala::Outro: return "Outro";
            }
            return "?";
        };

        auto tiposStr = [&](const std::vector<TipoSala>& tipos) {
            if (tipos.empty()) return std::string("?");
            std::string outTipos;
            for (std::size_t i = 0; i < tipos.size(); ++i) {
                if (i > 0) outTipos += '+';
                outTipos += tipoStr(tipos[i]);
            }
            return outTipos;
        };

        auto hmm = [](int minutos) {
            std::ostringstream os;
            os << (minutos / 60 < 10 ? "0" : "") << (minutos / 60)
               << ':'
               << (minutos % 60 < 10 ? "0" : "") << (minutos % 60);
            return os.str();
        };

        for (int oc = 0; oc < static_cast<int>(inst.ocorrencias.size()); ++oc) {
            const auto& o = inst.ocorrencias[oc];
            const auto& t = inst.turmas[o.idxTurma];
            const int s = sol.alocacao[oc];

            std::string codSala;
            std::string nomeSala;
            std::string tipoSalaAlocada;
            int capSala = 0;
            if (s >= 0 && s < static_cast<int>(inst.salas.size())) {
                codSala = inst.salas[s].codigo;
                nomeSala = inst.salas[s].nomeOriginal;
                tipoSalaAlocada = tipoStr(inst.salas[s].tipo);
                capSala = inst.salas[s].capacidade;
            }

            out << oc << ','
                << t.idTurma << ','
                << '"' << t.disciplina << '"' << ','
                << nomeDia(o.diaSemana) << ','
                << hmm(o.horario.inicio) << ','
                << hmm(o.horario.fim) << ','
                << '"' << tiposStr(o.tiposPermitidos) << '"' << ','
                << '"' << codSala << '"' << ','
                << '"' << nomeSala << '"' << ','
                << '"' << tipoSalaAlocada << '"' << ','
                << t.inscritos << ','
                << capSala << ','
                << sol.custo << ','
                << sol.inviabilidades << '\n';
        }

        (void)cfg;
        return true;
    } catch (...) {
        (void)cfg;
        return false;
    }
}
