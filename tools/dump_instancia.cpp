#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "Instancia.h"
#include "Solucao.h"
#include "Solver.h"
#include "Utils.h"
#include "parser.h"

namespace {

std::string tipoStr(TipoSala t) {
    switch (t) {
        case TipoSala::Sala: return "Sala";
        case TipoSala::LabInformatica: return "LabInformatica";
        case TipoSala::LabEspecifico: return "LabEspecifico";
        case TipoSala::Outro: return "Outro";
    }
    return "?";
}

int demandaTurma(const Turma& t) {
    if (t.inscritos > 0) return t.inscritos;
    if (t.vagas > 0) return t.vagas;
    return 0;
}

std::string csvCampo(const std::string& s) {
    const bool precisaAspas = s.find_first_of(",\"\n\r") != std::string::npos;
    if (!precisaAspas) return s;

    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

std::string hmm(int minutos) {
    std::ostringstream os;
    os << std::setfill('0') << std::setw(2) << (minutos / 60) << ':'
       << std::setfill('0') << std::setw(2) << (minutos % 60);
    return os.str();
}

std::ofstream abrir(const std::filesystem::path& dir, const std::string& nome) {
    std::ofstream out(dir / nome);
    if (!out) throw std::runtime_error("nao foi possivel gravar " + (dir / nome).string());
    return out;
}

void gravarSalas(const std::filesystem::path& dir, const Instancia& inst) {
    auto out = abrir(dir, "salas.csv");
    out << "idx,codigo,nome_original,unidade,andar,capacidade,tipo,acessibilidade,disponivel\n";
    for (const auto& s : inst.salas) {
        out << s.idx << ','
            << csvCampo(s.codigo) << ','
            << csvCampo(s.nomeOriginal) << ','
            << csvCampo(s.unidade) << ','
            << s.andar << ','
            << s.capacidade << ','
            << tipoStr(s.tipo) << ','
            << (s.acessibilidade ? 1 : 0) << ','
            << (s.disponivel ? 1 : 0) << '\n';
    }
}

void gravarTurmas(const std::filesystem::path& dir, const Instancia& inst) {
    auto out = abrir(dir, "turmas.csv");
    out << "idx,id_turma,codigo_uc,disciplina,subgrupo,termo,vagas,inscritos,"
           "demanda,docente,departamento,acessibilidade\n";
    for (const auto& t : inst.turmas) {
        out << t.idx << ','
            << t.idTurma << ','
            << csvCampo(t.codigoUc) << ','
            << csvCampo(t.disciplina) << ','
            << csvCampo(t.subgrupo) << ','
            << t.termo << ','
            << t.vagas << ','
            << t.inscritos << ','
            << demandaTurma(t) << ','
            << csvCampo(t.docente) << ','
            << csvCampo(t.departamento) << ','
            << (t.acessibilidade ? 1 : 0) << '\n';
    }
}

void gravarOcorrencias(const std::filesystem::path& dir, const Instancia& inst,
                       const Solucao& sol) {
    auto out = abrir(dir, "ocorrencias.csv");
    out << "idx,idx_turma,dia,inicio,fim,slot,tipo_requerido\n";
    for (std::size_t i = 0; i < inst.ocorrencias.size(); ++i) {
        const auto& o = inst.ocorrencias[i];
        out << i << ','
            << o.idxTurma << ','
            << nomeDia(o.diaSemana) << ','
            << hmm(o.horario.inicio) << ','
            << hmm(o.horario.fim) << ','
            << sol.slotDaOcorrencia[i] << ','
            << tipoStr(o.tipoSalaRequerido) << '\n';
    }
}

int gravarDominios(const std::filesystem::path& dir, const Instancia& inst) {
    auto out = abrir(dir, "dominios.csv");
    out << "idx_ocorrencia,idx_sala\n";

    int semDominio = 0;
    for (std::size_t i = 0; i < inst.ocorrencias.size(); ++i) {
        const auto& permitidas = inst.ocorrencias[i].salasPermitidas;
        if (permitidas.empty()) ++semDominio;
        for (int s : permitidas) out << i << ',' << s << '\n';
    }
    return semDominio;
}

void gravarDistancias(const std::filesystem::path& dir, const Instancia& inst) {
    auto out = abrir(dir, "distancias.csv");
    out << "idx_sala_i,idx_sala_j,distancia,conhecida\n";

    const int n = static_cast<int>(inst.salas.size());
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const int d = inst.distSalas[i][j];
            const bool conhecida = d < DIST_INF;
            out << i << ',' << j << ',' << (conhecida ? d : 0) << ','
                << (conhecida ? 1 : 0) << '\n';
        }
    }
}

void gravarParametros(const std::filesystem::path& dir, const Instancia& inst,
                      const SolverConfig& cfg) {
    auto out = abrir(dir, "parametros.csv");
    out << "chave,valor\n";

    const std::vector<std::pair<std::string, long long>> params = {
        {"pesoConsistenciaTurmaTipo", cfg.pesoConsistenciaTurmaTipo},
        {"pesoDistancia", cfg.pesoDistancia},
        {"pesoCapacidadeSobra", cfg.pesoCapacidadeSobra},
        {"pesoCapacidadeExcesso", cfg.pesoCapacidadeExcesso},
        {"penalidadeDistDesconhecida", cfg.penalidadeDistDesconhecida},
        {"escalaNormalizacao", cfg.escalaNormalizacao},
        {"normalizadorConsistencia", cfg.normalizadorConsistencia},
        {"normalizadorDistancia", cfg.normalizadorDistancia},
        {"normalizadorCapacidadeExcesso", cfg.normalizadorCapacidadeExcesso},
        {"normalizadorCapacidadeSobra", cfg.normalizadorCapacidadeSobra},
        {"normalizarCustosSuaves", cfg.normalizarCustosSuaves ? 1 : 0},
        {"normalizadorPorRange", cfg.normalizadorPorRange ? 1 : 0},
        {"duracaoPadraoMin", inst.duracaoPadraoMin},
    };

    for (const auto& [chave, valor] : params) out << chave << ',' << valor << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path dirSaida = (argc > 1) ? argv[1] : "data/dump";

    CaminhosCSV caminhos;
    caminhos.duracaoPadraoMin = 120;
    caminhos.salas = "data/Salas Oficiais.csv";
    caminhos.grad = "data/grad_instancia.csv";
    caminhos.mapeamento = "data/mapeamento.csv";
    caminhos.adjacencias = { "data/Matriz_Adjacencia_2Andar.csv", "data/Matriz_Adjacencia_3Andar.csv", "data/Matriz_Adjacencia_4Andar.csv" };

    RelatorioParse rel;
    Instancia inst;
    try {
        inst = parse(caminhos, &rel);
    } catch (const std::exception& e) {
        std::cerr << "Erro ao fazer parse: " << e.what() << '\n';
        return 1;
    }

    SolverConfig cfg;
    definirNormalizadoresPorRange(inst, cfg);

    const Solucao sol(inst);

    int semDominio = 0;
    try {
        std::filesystem::create_directories(dirSaida);
        gravarSalas(dirSaida, inst);
        gravarTurmas(dirSaida, inst);
        gravarOcorrencias(dirSaida, inst, sol);
        semDominio = gravarDominios(dirSaida, inst);
        gravarDistancias(dirSaida, inst);
        gravarParametros(dirSaida, inst, cfg);
    } catch (const std::exception& e) {
        std::cerr << "Erro ao gravar o dump: " << e.what() << '\n';
        return 1;
    }

    std::size_t paresDominio = 0;
    for (const auto& o : inst.ocorrencias) paresDominio += o.salasPermitidas.size();

    int paresSemDistancia = 0;
    const int nSalas = static_cast<int>(inst.salas.size());
    for (int i = 0; i < nSalas; ++i) {
        for (int j = i + 1; j < nSalas; ++j) {
            if (inst.distSalas[i][j] >= DIST_INF) ++paresSemDistancia;
        }
    }

    std::cout << "Dump gravado em: " << dirSaida.string() << '\n';
    std::cout << " salas: " << inst.salas.size() << '\n';
    std::cout << " turmas: " << inst.turmas.size() << '\n';
    std::cout << " ocorrencias: " << inst.ocorrencias.size() << '\n';
    std::cout << " slots: " << sol.numeroSlots() << '\n';
    std::cout << " pares (ocorrencia, sala) permitidos: " << paresDominio << '\n';
    std::cout << " pares de salas sem distancia conhecida: " << paresSemDistancia << " (usarao penalidadeDistDesconhecida = " << cfg.penalidadeDistDesconhecida << ")\n";

    if (semDominio > 0) {
        std::cout << "\nAviso: " << semDominio
                  << " ocorrencia(s) sem nenhuma sala permitida. O modelo exato"
                     " sera infactivel a menos que sejam tratadas\n";
    }

    if (!rel.avisos.empty()) {
        std::cout << "\nAvisos do parse (" << rel.avisos.size() << ")\n";
        for (const auto& a : rel.avisos) std::cout << "  * " << a << '\n';
    }

    return 0;
}
