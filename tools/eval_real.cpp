#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "Instancia.h"
#include "Solucao.h"
#include "Solver.h"
#include "Utils.h"
#include "parser.h"

static void imprimirRelatorio(const std::string& titulo, const RelatorioCusto& r, const SolverConfig& cfg) {
    std::cout << titulo << "\n";
    std::cout << " Custos suaves: " << (cfg.normalizarCustosSuaves ? "normalizados" : "brutos") << '\n';
    std::cout << " Custo total: " << r.custoTotal << '\n';
    std::cout << " Inviabilidades: " << r.inviabilidadesTotais << '\n';
    std::cout << " nao alocadas: " << r.ocorrenciasNaoAlocadas << '\n';
    std::cout << " conflitos slot+sala: " << r.conflitos << '\n';
    std::cout << " fora do dominio: " << r.foraDominio << '\n';
    std::cout << " tipo incompativel: " << r.tipoIncompativel << '\n';
    std::cout << " acessibilidade violada: " << r.acessibilidadeViolada << '\n';
    std::cout << " salas com capacidade excedida: " << r.capacidadeExcedida << " (excesso total: " << r.somaExcessoCapacidade << ")\n";
    std::cout << " Custo consistencia: " << r.custoConsistencia << '\n';
    std::cout << " Custo distancia: " << r.custoDistancia << " (soma dist. intra-turma: " << r.somaDistanciaIntraTurma << ")\n";
    std::cout << " Turmas tipo unico c/ salas diferentes: " << r.turmasTipoUnicoSalasDiferentes << "/" << r.turmasTipoUnico << '\n';
    std::cout << " Custo capacidade: " << r.custoCapacidade << " (sobra total: " << r.somaSobraCapacidade << ", excesso total: " << r.somaExcessoCapacidade << ")\n";
}

int main(int argc, char** argv) {
    const std::string caminhoAlocacao = (argc > 1) ? argv[1] : "data/agendamento_real.csv";
    CaminhosCSV caminhos;
    caminhos.duracaoPadraoMin = 120;
    caminhos.salas = "data/Salas Oficiais.csv";
    caminhos.grad = "data/grad_instancia.csv";
    caminhos.mapeamento = "data/mapeamento.csv";
    caminhos.adjacencias = { "data/Matriz_Adjacencia_2Andar.csv", "data/Matriz_Adjacencia_3Andar.csv", "data/Matriz_Adjacencia_4Andar.csv" };

    SolverConfig cfg; // defaults

    RelatorioParse rel;
    Instancia inst = parse(caminhos, &rel);

    definirNormalizadoresPorRange(inst, cfg);

    const int nOcc = static_cast<int>(inst.ocorrencias.size());
    std::cout << "Ocorrencias na instancia (grad_instancia): " << nOcc << '\n';

    // mapa codigo_sala -> idx
    std::unordered_map<std::string, int> codParaIdx;
    for (const auto& s : inst.salas) codParaIdx[s.codigo] = s.idx;

    std::ifstream in(caminhoAlocacao);
    if (!in) { std::cerr << "nao abriu " << caminhoAlocacao << '\n'; return 1; }
    std::cout << "Alocacao avaliada: " << caminhoAlocacao << '\n';

    Solucao sol(inst);
    std::string linha;

    if (!std::getline(in, linha)) { std::cerr << "arquivo vazio: " << caminhoAlocacao << '\n'; return 1; }
    const auto cabecalho = splitCSV(removerBOM(linha), ',');

    int colIdx = -1, colCod = -1;
    for (std::size_t i = 0; i < cabecalho.size(); ++i) {
        const std::string nome = trim(cabecalho[i]);
        if (nome == "idx_ocorrencia") colIdx = static_cast<int>(i);
        else if (nome == "codigo_sala") colCod = static_cast<int>(i);
    }
    if (colIdx < 0 || colCod < 0) {
        std::cerr << "cabecalho de " << caminhoAlocacao
                  << " precisa das colunas 'idx_ocorrencia' e 'codigo_sala'\n";
        return 1;
    }
    const int colMax = std::max(colIdx, colCod);

    int lidos = 0, salaInexistente = 0, idxForaRange = 0;
    int maxIdx = -1;
    while (std::getline(in, linha)) {
        linha = removerBOM(linha);
        if (trim(linha).empty()) continue;
        auto campos = splitCSV(linha, ',');
        if (static_cast<int>(campos.size()) <= colMax) continue;
        int idx = std::stoi(trim(campos[colIdx]));
        std::string cod = trim(campos[colCod]);
        ++lidos;
        maxIdx = std::max(maxIdx, idx);
        if (idx < 0 || idx >= nOcc) { ++idxForaRange; continue; }
        auto it = codParaIdx.find(cod);
        if (it == codParaIdx.end()) {
            ++salaInexistente;
            std::cerr << "AVISO: idx " << idx << " sala '" << cod << "' inexistente nas salas oficiais\n";
            continue;
        }
        sol.alocacao[idx] = it->second;
    }

    std::cout << "Linhas lidas da alocacao: " << lidos << '\n';
    std::cout << "Maior idx_ocorrencia no arquivo: " << maxIdx << '\n';
    std::cout << "idx fora do range [0," << nOcc-1 << "]: " << idxForaRange << '\n';
    std::cout << "codigo_sala inexistente: " << salaInexistente << '\n';

    int naoAtribuidas = 0;
    for (int oc = 0; oc < nOcc; ++oc) if (sol.alocacao[oc] < 0) ++naoAtribuidas;
    std::cout << "Ocorrencias sem sala apos carregar real: " << naoAtribuidas << "\n\n";

    imprimirRelatorio("Relatorio do agendamento real:", computarRelatorio(sol, inst, cfg), cfg);

    auto tipoStr = [](TipoSala t){ 
      switch(t) { 
        case TipoSala::Sala:return "Sala";
        case TipoSala::LabInformatica:return "LabInformatica";
        case TipoSala::LabEspecifico:return "LabEspecifico";
        default: return "Outro";
      } 
    };

    auto hmm = [](int m){ 
      std::ostringstream os; 
      os<<std::setfill('0') <<std::setw(2) << m/60 <<':' << std::setfill('0') << std::setw(2) << m%60; 
      return os.str(); 
    };

    return 0;
}
