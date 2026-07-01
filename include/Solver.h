#pragma once

#include <string>
#include <utility>
#include "Instancia.h"
#include "Solucao.h"

enum class Metaheuristica { VNS, ILS };

struct SolverConfig {
    int seed = 873654221;
    int maxIters = 15000;
    int maxNoImprove = 5000;
    int shakeStrength = 4;
    int verbose = 1;

    Metaheuristica metodo = Metaheuristica::ILS;
    int numStarts = 20;
    double tempoLimiteSegundos = 0;
    double probAceitarPioraILS = 0.05;
    bool normalizarCustosSuaves = true;
    int escalaNormalizacao = 10000;

    std::string arquivoSementes = "Sementes_Taillard.txt";
    bool sortearSementes = true;

    int ilsReinicioSemMelhora = 0;

    bool ilsPerturbacaoAdaptativa = true;
    int ilsShakeMax = 0;
    int ilsItersPorNivel = 0;

    int pesoConsistenciaTurmaTipo = 10;
    int pesoDistancia = 1;
    int pesoCapacidadeSobra = 1;
    int pesoCapacidadeExcesso = 10;

    int penalidadeDistDesconhecida = 3000;

    bool normalizadorPorRange = true;
    int normalizadorConsistencia = 2000;
    int normalizadorDistancia = 50000;
    int normalizadorCapacidade = 10000;
};

struct SolverStats {
    int iteracoes = 0;
    int perturbacoes = 0;
    int starts = 0;
    double tempoDecorrido = 0.0;

    int inicialInviabilidades = 0;
    int inicialCusto = 0;

    int melhorInviabilidades = 0;
    int melhorCusto = 0;
    int ocorrenciasAlocadas = 0;
};

struct RelatorioCusto {
    int custoTotal = 0;
    int inviabilidadesTotais = 0;

    int ocorrenciasNaoAlocadas = 0;
    int conflitos = 0;
    int foraDominio = 0;
    int tipoIncompativel = 0;
    int acessibilidadeViolada = 0;
    int capacidadeExcedida = 0;

    int custoConsistencia = 0;
    int custoDistancia = 0;
    int custoCapacidade = 0;

    long long somaDistanciaIntraTurma = 0;
    long long somaSobraCapacidade = 0;
    long long somaExcessoCapacidade = 0;

    int turmasTipoUnicoSalasDiferentes = 0;
    int turmasTipoUnico = 0;
};

void definirNormalizadoresPorRange(const Instancia& inst, SolverConfig& cfg);
RelatorioCusto computarRelatorio(const Solucao& sol, const Instancia& inst, const SolverConfig& cfg);
void avaliarSolucao(Solucao& sol, const Instancia& inst, const SolverConfig& cfg);
Solucao construirSolucaoInicialGulosa(const Instancia& inst, const SolverConfig& cfg);
std::pair<Solucao, SolverStats> executar(const Instancia& inst, const SolverConfig& cfg);
bool escreverAlocacaoCSV(const std::string& path, const Solucao& sol, const Instancia& inst, const SolverConfig& cfg);
