#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "Instancia.h"
#include "Solucao.h"
#include "Solver.h"
#include "Utils.h"
#include "parser.h"

static std::string tipoStr(TipoSala t) {
    switch (t) {
        case TipoSala::Sala: return "Sala";
        case TipoSala::LabInformatica: return "LabInformatica";
        case TipoSala::LabEspecifico: return "LabEspecifico";
        case TipoSala::Outro: return "Outro";
    }
    return "?";
}

static std::string hmm(int minutos) {
    std::ostringstream os;
    os << std::setfill('0') << std::setw(2) << (minutos / 60) << ':' << std::setfill('0') << std::setw(2) << (minutos % 60);
    return os.str();
}

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
    CaminhosCSV caminhos;
    caminhos.duracaoPadraoMin = 120; // duracao padrao de 2h de aula
    caminhos.salas = "data/Salas Oficiais.csv";
    caminhos.grad = "data/grad_instancia.csv";
    caminhos.mapeamento = "data/mapeamento.csv";
    caminhos.adjacencias = { "data/Matriz_Adjacencia_2Andar.csv", "data/Matriz_Adjacencia_3Andar.csv", "data/Matriz_Adjacencia_4Andar.csv" };

    SolverConfig cfg;
    const std::string outPath = "results/alocacao.csv";

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--ils") cfg.metodo = Metaheuristica::ILS;
        else if (a == "--vns") cfg.metodo = Metaheuristica::VNS;
        else std::cerr << "arg desconhecido: " << a << '\n';
    }

    RelatorioParse rel;
    Instancia inst;
    try {
        inst = parse(caminhos, &rel);
    } catch (const std::exception& e) {
        std::cerr << "Erro ao fazer parse: " << e.what() << '\n';
        return 1;
    }

    definirNormalizadoresPorRange(inst, cfg);

    std::cout << "Resumo da instancia: \n";
    std::cout << "Salas lidas: " << rel.salasLidas << '\n';
    std::cout << "Salas ignoradas: " << rel.salasIgnoradas << '\n';
    std::cout << "Turmas lidas: " << rel.turmasLidas << '\n';
    std::cout << "Turmas inconsistentes: " << rel.turmasComInconsistencia << '\n';
    std::cout << "Ocorrencias lidas: " << rel.ocorrenciasLidas << '\n';
    std::cout << "Ocorrencias sem tipo: " << rel.ocorrenciasComTipoVazio << '\n';
    std::cout << "Pares com distancia: " << rel.paresComDistancia << '\n';
    std::cout << "Pares sem distancia: " << rel.paresSemDistancia << '\n';
    std::cout << "Adjacencias inconsistentes: " << rel.adjacenciasInconsistentes << "\n\n";

    std::map<TipoSala, int> porTipo;
    for (const auto& s : inst.salas) porTipo[s.tipo]++;
    std::cout << "Salas por tipo\n";
    for (const auto& [t, n] : porTipo) {
        std::cout << "  " << std::setw(16) << std::left << tipoStr(t) << " : " << n << '\n';
    }
    std::cout << '\n';

    std::map<TipoSala, int> ocPorTipo;
    for (const auto& o : inst.ocorrencias) ocPorTipo[o.tipoSalaRequerido]++;
    std::cout << "Ocorrencias por tipo de sala\n";
    for (const auto& [t, n] : ocPorTipo) {
        std::cout << "  " << std::setw(16) << std::left << tipoStr(t) << " : " << n << '\n';
    }
    std::cout << '\n';

    int capSala = 0, capLI = 0, capLE = 0;
    for (const auto& s : inst.salas) {
        if (!s.disponivel) continue;
        if (s.tipo == TipoSala::Sala) ++capSala;
        else if (s.tipo == TipoSala::LabInformatica) ++capLI;
        else if (s.tipo == TipoSala::LabEspecifico) ++capLE;
    }

    std::map<std::string, std::map<TipoSala, int>> demanda;
    for (const auto& o : inst.ocorrencias) {
        const auto k = nomeDia(o.diaSemana) + " " + hmm(o.horario.inicio);
        demanda[k][o.tipoSalaRequerido]++;
    }

    std::cout << "Distribuicao de ocorrencias por slot\n";
    std::cout << "Capacidade total: salas=" << capSala << " labInf=" << capLI << " labEsp=" << capLE << '\n';
    for (const auto& [slot, d] : demanda) {
        const int s = d.count(TipoSala::Sala) ? d.at(TipoSala::Sala) : 0;
        const int li = d.count(TipoSala::LabInformatica) ? d.at(TipoSala::LabInformatica) : 0;
        const int le = d.count(TipoSala::LabEspecifico) ? d.at(TipoSala::LabEspecifico) : 0;
        const bool alerta = s > capSala || li > capLI || le > capLE;
        std::cout << "  " << std::setw(12) << std::left << slot << " S=" << std::setw(3) << s << " LI=" << std::setw(3) << li
                  << " LE=" << std::setw(3) << le
                  << (alerta ? "  <-- verificar" : "") << '\n';
    }
    std::cout << '\n';

    if (!rel.avisos.empty()) {
        std::cout << "Avisos (" << rel.avisos.size() << ")\n";
        for (const auto& a : rel.avisos) std::cout << "  * " << a << '\n';
        std::cout << '\n';
    }

    Solucao sol(inst);
    std::cout << "Solucao inicializada: " << sol.alocacao.size() << " ocorrencias, " << sol.numeroSlots() << " slots distintos, " << sol.turmasPorGrupo.size() << " grupos\n";

    std::cout << "Metaheuristica: " << (cfg.metodo == Metaheuristica::ILS ? "ILS" : "VNS") << '\n';
    auto [best, stats] = executar(inst, cfg);

    std::cout << "Inviabilidades iniciais: " << stats.inicialInviabilidades << '\n';
    std::cout << "Custo inicial: " << stats.inicialCusto << '\n';
    std::cout << "Melhor inviabilidade: " << stats.melhorInviabilidades << '\n';
    std::cout << "Melhor custo: " << stats.melhorCusto << '\n';
    std::cout << "Ocorrencias alocadas: " << stats.ocorrenciasAlocadas << "/" << inst.ocorrencias.size() << '\n';
    std::cout << "Perturbacoes: " << stats.perturbacoes << '\n';
    std::cout << "Tempo (s): " << std::fixed << std::setprecision(3) << stats.tempoDecorrido << '\n';

    imprimirRelatorio("Relatorio da solucao gerada", computarRelatorio(best, inst, cfg), cfg);

    if (escreverAlocacaoCSV(outPath, best, inst, cfg)) {
        std::cout << "CSV da melhor alocacao salvo em: " << outPath << '\n';
    } else {
        std::cout << "Falha ao salvar CSV em: " << outPath << '\n';
    }

    return 0;
}
