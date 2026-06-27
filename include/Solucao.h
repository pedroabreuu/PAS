#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Instancia.h"

struct Solucao {
    std::vector<int> alocacao; // solucao[ocorrencia] = sala
    std::vector<int> slotDaOcorrencia;
    std::vector<std::vector<int>> ocupacao;
    std::vector<std::vector<int>> ocorrenciasDaTurma;
    std::vector<std::vector<int>> turmasPorGrupo;
    std::vector<std::pair<std::string, int>> chavesGrupo;
    std::vector<int> grupoDaTurma;

    int custo = 0;
    int inviabilidades = 0;

    Solucao() = default;
    explicit Solucao(const Instancia& inst) { inicializar(inst); }

    void inicializar(const Instancia& inst) {
        const int nOcc = static_cast<int>(inst.ocorrencias.size());
        const int nSalas = static_cast<int>(inst.salas.size());
        const int nTurmas = static_cast<int>(inst.turmas.size());

        alocacao.assign(nOcc, -1);
        slotDaOcorrencia = construirSlots(inst);

        int totalSlots = 0;
        for (int s : slotDaOcorrencia) totalSlots = std::max(totalSlots, s + 1);
        ocupacao.assign(totalSlots, std::vector<int>(nSalas, -1));
        ocorrenciasDaTurma.assign(nTurmas, {}); // indice das ocorrencias de cada turma
        grupoDaTurma.assign(nTurmas, -1); // indice do grupo de cada turma
        turmasPorGrupo.clear();
        chavesGrupo.clear();

        for (int i = 0; i < nOcc; ++i) {
            const int t = inst.ocorrencias[i].idxTurma;
            if (t >= 0 && t < nTurmas) ocorrenciasDaTurma[t].push_back(i); // mapeia turma -> ocorrencias
        }

        std::unordered_map<std::string, int> mapaGrupo;
        for (int t = 0; t < nTurmas; ++t) {
            const std::string chave = inst.turmas[t].departamento + "#" + // agrupa departamento + termo
                                      std::to_string(inst.turmas[t].termo);
            auto it = mapaGrupo.find(chave); // verifica se o grupo ja existe
            int g = -1; // se nao existe, cria novo grupo

            if (it == mapaGrupo.end()) {
                g = static_cast<int>(turmasPorGrupo.size());
                mapaGrupo.emplace(chave, g);
                turmasPorGrupo.push_back({});
                chavesGrupo.push_back({inst.turmas[t].departamento, inst.turmas[t].termo});
            } else {
                g = it->second;
            }
            grupoDaTurma[t] = g;
            turmasPorGrupo[g].push_back(t);
        }
    }

    int numeroSlots() const { return static_cast<int>(ocupacao.size()); } // numero de slots de tempo distintos (dia+horario) na instancia

    bool estaAlocada(int idxOcorrencia) const { // verifica se a ocorrencia esta alocada em alguma sala
        return idxOcorrencia >= 0 &&
               idxOcorrencia < static_cast<int>(alocacao.size()) &&
               alocacao[idxOcorrencia] != -1;
    }

    int salaDaOcorrencia(int idxOcorrencia) const { // retorna a sala em que a ocorrencia esta alocada
        if (idxOcorrencia < 0 || idxOcorrencia >= static_cast<int>(alocacao.size())) {
            throw std::out_of_range("idxOcorrencia invalido em salaDaOcorrencia");
        }
        return alocacao[idxOcorrencia];
    }

    bool slotLivre(int idxSlot, int idxSala) const {
        return ocupacao.at(idxSlot).at(idxSala) == -1;
    }

    void remover(int idxOcorrencia) {
        if (!estaAlocada(idxOcorrencia)) return;
        const int idxSala = alocacao[idxOcorrencia];
        const int idxSlot = slotDaOcorrencia.at(idxOcorrencia);
        ocupacao.at(idxSlot).at(idxSala) = -1;
        alocacao[idxOcorrencia] = -1;
    }

    void atribuir(int idxOcorrencia, int idxSala) { // atribui a ocorrencia a sala, assumindo que o slot esta livre
        const int idxSlot = slotDaOcorrencia.at(idxOcorrencia);
        remover(idxOcorrencia);
        ocupacao.at(idxSlot).at(idxSala) = idxOcorrencia;
        alocacao[idxOcorrencia] = idxSala;
    }

private:
    static std::vector<int> construirSlots(const Instancia& inst) { // constroi o vetor slotDaOcorrencia, mapeando cada ocorrencia para um slot de tempo unico (dia+horario)
        std::vector<int> slots(inst.ocorrencias.size(), -1);
        std::unordered_map<long long, int> chaveParaIdx;
        auto encode = [](int dia, int ini, int fim) -> long long {
            return (static_cast<long long>(dia) << 32) |
                   (static_cast<long long>(ini) << 16) |
                   static_cast<long long>(fim);
        };
        for (std::size_t i = 0; i < inst.ocorrencias.size(); ++i) {
            const auto& o = inst.ocorrencias[i];
            const long long chave = encode(static_cast<int>(o.diaSemana), o.horario.inicio, o.horario.fim); // chave horario -> indice slot
            auto it = chaveParaIdx.find(chave);
            if (it == chaveParaIdx.end()) { // se a chave nao existe no mapa
                const int idx = static_cast<int>(chaveParaIdx.size()); // cria novo slot
                chaveParaIdx.emplace(chave, idx);
                slots[i] = idx;
            } else {
                slots[i] = it->second; // indice do slot
            }
        }
        return slots;
    }
};