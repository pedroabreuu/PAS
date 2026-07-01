#include "parser.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "Utils.h"

namespace {

int extrairPrimeiroNumero(const std::string& s) {
    std::string buf;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            buf.push_back(c);
        } else if (!buf.empty()) {
            break;
        }
    }
    if (buf.empty()) return -1;
    try { return std::stoi(buf); } catch (...) { return -1; }
}

int inferirAndar(int numero) { // define o andar das salas
    if (numero < 0) return -1;
    if (numero == 20) return 0;
    if (numero >= 200 && numero < 300) return 2;
    if (numero >= 300 && numero < 400) return 3;
    if (numero >= 400 && numero < 500) return 4;
    return -1;
}

TipoSala classificarTipoSalasOficiais(const std::string& nomeBruto) {
    const auto n = normalizar(nomeBruto);
    if (n.rfind("sala", 0) == 0) return TipoSala::Sala;
    if (n.find("inf") != std::string::npos) return TipoSala::LabInformatica;
    if (n.rfind("lab", 0) == 0) return TipoSala::LabEspecifico;
    return TipoSala::Outro;
}

// le Salas Oficiais.csv formato Unidade, Sala/Laboratorio, Lugares
std::vector<Sala> parseSalas(const std::string& path, RelatorioParse* rel) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Falha ao abrir salas: " + path);

    std::string header;
    if (!std::getline(in, header)) throw std::runtime_error("salas vazio: " + path);
    header = removerBOM(header);
    const char delim = detectaDelimitador(header);
    const auto cabecalho = splitCSV(header, delim);

    std::unordered_map<std::string, int> col;
    for (std::size_t i = 0; i < cabecalho.size(); ++i)
        col[normalizar(cabecalho[i])] = static_cast<int>(i);

    auto colunaIdx = [&](std::initializer_list<const char*> opcoes) -> int {
        for (const char* o : opcoes) {
            const auto it = col.find(normalizar(o));
            if (it != col.end()) return it->second;
        }
        return -1;
    };

    const int cUnid = colunaIdx({"Unidade"});
    const int cNome = colunaIdx({"Sala/Laboratório", "Sala/Laboratorio", "Sala"});
    const int cLugares = colunaIdx({"Lugares", "Capacidade"});
    const int cAcess = colunaIdx({"Acessibilidade", "PCD"});

    if (cNome < 0) throw std::runtime_error("salas sem coluna de nome");

    std::vector<Sala> salas;
    std::string linha;
    while (std::getline(in, linha)) {
        linha = removerBOM(linha);
        if (trim(linha).empty()) continue;
        const auto campos = splitCSV(linha, delim);
        if (static_cast<int>(campos.size()) <= cNome) continue;

        const std::string nome = trim(campos[cNome]);
        if (nome.empty()) continue;

        Sala s;
        s.nomeOriginal = nome;
        s.unidade = (cUnid >= 0 && cUnid < static_cast<int>(campos.size())) ? trim(campos[cUnid]) : "";
        s.tipo = classificarTipoSalasOficiais(nome);

        if (s.tipo == TipoSala::Outro) {
            if (rel) rel->salasIgnoradas++;
            continue;
        }

        const int numero = extrairPrimeiroNumero(nome);
        s.andar = inferirAndar(numero);
        s.codigo = (numero >= 0 ? std::to_string(numero) : normalizar(nome));

        int cap = 0;
        if (cLugares >= 0 && cLugares < static_cast<int>(campos.size())) {
            const auto t = trim(campos[cLugares]);
            if (!t.empty()) {
                try { cap = std::stoi(t); } catch (...) { cap = 0; }
            }
        }
        s.capacidade = cap;

        if (cAcess >= 0 && cAcess < static_cast<int>(campos.size())) {
            const auto a = normalizar(campos[cAcess]);
            s.acessibilidade = (a == "1" || a == "sim" || a == "true");
        }

        salas.push_back(std::move(s));
    }

    for (std::size_t i = 0; i < salas.size(); ++i)
        salas[i].idx = static_cast<int>(i);
    if (rel) rel->salasLidas = static_cast<int>(salas.size());
    return salas;
}

struct ParseGradResult {
    std::vector<Turma> turmas;
    std::vector<Ocorrencia> ocorrencias;
};

ParseGradResult parseGrad(const std::string& path, int duracaoMin, RelatorioParse* rel) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Falha ao abrir grad: " + path);

    std::string header;
    if (!std::getline(in, header)) throw std::runtime_error("grad vazio: " + path);
    header = removerBOM(header);
    const char delim = detectaDelimitador(header);
    const auto cabecalho = splitCSV(header, delim);

    std::unordered_map<std::string, int> col;
    for (std::size_t i = 0; i < cabecalho.size(); ++i)
        col[normalizar(cabecalho[i])] = static_cast<int>(i);

    auto colunaIdx = [&](std::initializer_list<const char*> opcoes) -> int {
        for (const char* o : opcoes) {
            const auto it = col.find(normalizar(o));
            if (it != col.end()) return it->second;
        }
        return -1;
    };

    const int cId = colunaIdx({"ID_TURMA", "ID TURMA"});
    const int cTermo = colunaIdx({"TERMO"});
    const int cCodUc = colunaIdx({"CÓD. UC", "COD UC", "CÓD UC", "COD. UC"});
    const int cDisc = colunaIdx({"UNIDADE CURRICULAR", "DISCIPLINA"});
    const int cSubgrupo = colunaIdx({"TURMA"});
    const int cVagas = colunaIdx({"Vagas", "VAGAS"});
    const int cInscritos = colunaIdx({"Inscritos", "INSCRITOS"});
    const int cDocente = colunaIdx({"DOCENTE"});
    const int cDepto = colunaIdx({"DEPARTAMENTO", "DEPTO"});
    const int cTipoSala = colunaIdx({"TIPO_SALA", "TIPO SALA"});
    const int cDia = colunaIdx({"Dia", "DIA"});
    const int cHora = colunaIdx({"Hora", "HORA"});
    const int cAcess = colunaIdx({"Acessibilidade", "PCD"});

    if (cId < 0 || cDisc < 0 || cDia < 0 || cHora < 0) {
        throw std::runtime_error("grad sem colunas minimas (ID_TURMA/UNIDADE CURRICULAR/Dia/Hora)");
    }

    std::unordered_map<int, int> idParaIdx;
    std::unordered_set<int> idsComInconsistencia;
    ParseGradResult r;
    std::string linha;
    int numLinha = 1;
    while (std::getline(in, linha)) {
        ++numLinha;
        linha = removerBOM(linha);
        if (trim(linha).empty()) continue;
        const auto campos = splitCSV(linha, delim);
        if (static_cast<int>(campos.size()) <= cId) continue;

        auto pegar = [&](int c) -> std::string {
            if (c < 0 || c >= static_cast<int>(campos.size())) return "";
            return trim(campos[c]);
        };

        int idTurma = 0;
        try { idTurma = std::stoi(pegar(cId)); }
        catch (...) {
            if (rel) rel->avisos.push_back("grad linha " + std::to_string(numLinha) + ": ID_TURMA invalido");
            continue;
        }

        auto it = idParaIdx.find(idTurma);
        if (it == idParaIdx.end()) {
            Turma t;
            t.idx = static_cast<int>(r.turmas.size());
            t.idTurma = idTurma;
            t.termo = parseTermo(pegar(cTermo));
            t.codigoUc = pegar(cCodUc);
            t.disciplina = pegar(cDisc);
            t.subgrupo = pegar(cSubgrupo);
            try { t.vagas = std::stoi(pegar(cVagas)); } catch (...) { t.vagas = 0; }
            try { t.inscritos = std::stoi(pegar(cInscritos)); } catch (...) { t.inscritos = 0; }
            t.docente = pegar(cDocente);
            t.departamento = pegar(cDepto);
            idParaIdx[idTurma] = t.idx;
            r.turmas.push_back(std::move(t));
        } else {
            const Turma& t = r.turmas[it->second];
            auto avisaDif = [&](const std::string& campo, const std::string& valorBase, const std::string& valorNovo) {
                if (valorBase != valorNovo && idsComInconsistencia.insert(idTurma).second && rel) {
                    rel->turmasComInconsistencia++;
                }
                if (valorBase != valorNovo && rel) {
                    rel->avisos.push_back(
                        "grad linha " + std::to_string(numLinha) +
                        ": inconsistencia para ID_TURMA=" + std::to_string(idTurma) +
                        " no campo " + campo + " ('" + valorBase + "' vs '" + valorNovo + "')");
                }
            };
            avisaDif("TERMO", std::to_string(t.termo), std::to_string(parseTermo(pegar(cTermo))));
            avisaDif("COD_UC", t.codigoUc, pegar(cCodUc));
            avisaDif("DISCIPLINA", t.disciplina, pegar(cDisc));
            avisaDif("TURMA", t.subgrupo, pegar(cSubgrupo));
            avisaDif("DOCENTE", t.docente, pegar(cDocente));
            int vagas = 0;
            try { vagas = std::stoi(pegar(cVagas)); } catch (...) { vagas = 0; }
            avisaDif("VAGAS", std::to_string(t.vagas), std::to_string(vagas));
        }

        const int idxTurma = idParaIdx[idTurma];

        {
            const auto a = normalizar(pegar(cAcess));
            if (a == "1" || a == "sim" || a == "true") r.turmas[idxTurma].acessibilidade = true;
        }

        Ocorrencia o;
        o.idxTurma = idxTurma;
        try { o.diaSemana = parseDia(pegar(cDia)); }
        catch (const std::exception& e) {
            if (rel) rel->avisos.push_back("grad linha " + std::to_string(numLinha) + ": " + e.what());
            continue;
        }
        try {
            o.horario.inicio = parseHorario(pegar(cHora));
            o.horario.fim = o.horario.inicio + duracaoMin;
        } catch (const std::exception& e) {
            if (rel) rel->avisos.push_back("grad linha " + std::to_string(numLinha) + ": " + e.what());
            continue;
        }

        // tipo preliminar via coluna TIPO_SALA sera melhorado pelo mapeamento
        const auto ts = normalizar(pegar(cTipoSala));
        o.tipoSalaOriginal = ts;
        if (ts == "sl") {
            o.tipoSalaRequerido = TipoSala::Sala;
            o.tiposPermitidos = {TipoSala::Sala};
        } else if (ts == "labinfo") {
            o.tipoSalaRequerido = TipoSala::LabInformatica;
            o.tiposPermitidos = {TipoSala::LabInformatica};
        } else if (ts == "lab") {
            o.tipoSalaRequerido = TipoSala::LabEspecifico;
            o.tiposPermitidos = {TipoSala::LabEspecifico};
        } else {
            o.tipoSalaRequerido = TipoSala::Sala;
            o.tiposPermitidos = {TipoSala::Sala};
            if (rel) {
                rel->ocorrenciasComTipoVazio++;
                rel->avisos.push_back("grad linha " + std::to_string(numLinha) + ": TIPO_SALA vazio/desconhecido ('" + pegar(cTipoSala) +
                                      "'), assumido SL (ID_TURMA=" + std::to_string(idTurma) + ")");
            }
        }

        r.ocorrencias.push_back(o);
    }

    if (rel) {
        rel->turmasLidas = static_cast<int>(r.turmas.size());
        rel->ocorrenciasLidas = static_cast<int>(r.ocorrencias.size());
    }
    return r;
}

struct MapeamentoEntry {
    std::string codUc;
    std::string categoria;
    std::vector<std::string> salasPermitidas;
};

// le mapeamento.csv e retorna mapa COD_UC -> entrada
std::unordered_map<std::string, MapeamentoEntry> parseMapeamento(
    const std::string& path, RelatorioParse* rel) {
    std::unordered_map<std::string, MapeamentoEntry> mapa;

    std::ifstream in(path);
    if (!in) {
        if (rel) rel->avisos.push_back("mapeamento.csv nao encontrado: " + path);
        return mapa;
    }

    std::string header;
    if (!std::getline(in, header)) return mapa;
    header = removerBOM(header);
    const char delim = detectaDelimitador(header);
    const auto cabecalho = splitCSV(header, delim);

    std::unordered_map<std::string, int> col;
    for (std::size_t i = 0; i < cabecalho.size(); ++i)
        col[normalizar(cabecalho[i])] = static_cast<int>(i);

    auto colunaIdx = [&](std::initializer_list<const char*> opcoes) -> int {
        for (const char* o : opcoes) {
            const auto it = col.find(normalizar(o));
            if (it != col.end()) return it->second;
        }
        return -1;
    };

    const int cCod = colunaIdx({"COD_UC", "COD UC"});
    const int cCat = colunaIdx({"CATEGORIA"});
    const int cSalas = colunaIdx({"SALAS_PERMITIDAS", "SALAS PERMITIDAS"});

    if (cCod < 0 || cCat < 0 || cSalas < 0) {
        if (rel) rel->avisos.push_back("mapeamento.csv sem colunas esperadas");
        return mapa;
    }

    std::string linha;
    while (std::getline(in, linha)) {
        linha = removerBOM(linha);
        if (trim(linha).empty()) continue;
        const auto campos = splitCSV(linha, delim);

        auto pegar = [&](int c) -> std::string {
            if (c < 0 || c >= static_cast<int>(campos.size())) return "";
            return trim(campos[c]);
        };

        const std::string cod = pegar(cCod);
        if (cod.empty()) continue;

        MapeamentoEntry e;
        e.codUc = cod;
        e.categoria = normalizar(pegar(cCat));

        // salas_permitidas separadas por ;
        const std::string raw = pegar(cSalas);
        std::istringstream ss(raw);
        std::string token;
        while (std::getline(ss, token, ';')) {
            const auto t = trim(token);
            if (!t.empty()) e.salasPermitidas.push_back(t);
        }

        mapa[cod] = std::move(e);
    }

    return mapa;
}

// constroi dominio de salas permitidas por ocorrencia usando mapeamento.csv
void aplicarMapeamento(std::vector<Sala>& salas, const std::vector<Turma>& turmas, std::vector<Ocorrencia>& ocorrencias, const std::unordered_map<std::string, MapeamentoEntry>& mapa, RelatorioParse* rel) {
    std::unordered_map<std::string, int> porCodigo;
    for (const auto& s : salas) porCodigo[s.codigo] = s.idx;

    // indices por tipo para fallback
    std::vector<int> todasSalas, todosLabInf, todosLabEsp;
    for (const auto& s : salas) {
        if (!s.disponivel) continue;
        if (s.tipo == TipoSala::Sala) todasSalas.push_back(s.idx);
        else if (s.tipo == TipoSala::LabInformatica) todosLabInf.push_back(s.idx);
        else if (s.tipo == TipoSala::LabEspecifico) todosLabEsp.push_back(s.idx);
    }

    auto resolverSalas = [&](const std::vector<std::string>& codigos) -> std::vector<int> {
        std::vector<int> resultado;
        for (const auto& c : codigos) {
            const auto upper = normalizar(c);
            if (upper == "sala") {
                resultado.insert(resultado.end(), todasSalas.begin(), todasSalas.end());
            } else {
                auto it = porCodigo.find(c);
                if (it != porCodigo.end()) {
                    resultado.push_back(it->second);
                } else if (rel) {
                    rel->avisos.push_back("mapeamento aponta para sala '" + c + "' inexistente");
                }
            }
        }
        // remover duplicatas mantendo ordem
        std::unordered_set<int> visto;
        std::vector<int> unico;
        for (int idx : resultado) {
            if (visto.insert(idx).second) unico.push_back(idx);
        }
        std::sort(unico.begin(), unico.end());
        return unico;
    };

    auto contemTipo = [](const std::vector<TipoSala>& tipos, TipoSala tipo) {
        return std::find(tipos.begin(), tipos.end(), tipo) != tipos.end();
    };

    auto normalizarTipos = [](std::vector<TipoSala> tipos) {
        std::sort(tipos.begin(), tipos.end(), [](TipoSala a, TipoSala b) {
            return static_cast<int>(a) < static_cast<int>(b);
        });
        tipos.erase(std::unique(tipos.begin(), tipos.end()), tipos.end());
        return tipos;
    };

    auto tiposDaCategoria = [](const std::string& cat) {
        std::vector<TipoSala> tipos;
        if (cat == "sala") {
            tipos = {TipoSala::Sala};
        } else if (cat == "labinfo") {
            tipos = {TipoSala::LabInformatica};
        } else if (cat == "lab especifico") {
            tipos = {TipoSala::LabEspecifico};
        } else if (cat == "sala labinfo") {
            tipos = {TipoSala::Sala, TipoSala::LabInformatica};
        } else if (cat == "sala lab especifico") {
            tipos = {TipoSala::Sala, TipoSala::LabEspecifico};
        } else if (cat == "labinfo lab especifico") {
            tipos = {TipoSala::LabInformatica, TipoSala::LabEspecifico};
        }
        return tipos;
    };

    auto escolherTiposDaOcorrencia = [&](const Ocorrencia& oc, const std::vector<TipoSala>& tiposCategoria) {
        const auto raw = oc.tipoSalaOriginal;
        std::vector<TipoSala> tipos;

        if (raw == "sl" && contemTipo(tiposCategoria, TipoSala::Sala)) {
            tipos = {TipoSala::Sala};
        } else if (raw == "labinfo" && contemTipo(tiposCategoria, TipoSala::LabInformatica)) {
            tipos = {TipoSala::LabInformatica};
        } else if (raw == "lab") {
            const bool podeLabInf = contemTipo(tiposCategoria, TipoSala::LabInformatica);
            const bool podeLabEsp = contemTipo(tiposCategoria, TipoSala::LabEspecifico);
            if (podeLabInf && podeLabEsp) {
                tipos = {TipoSala::LabInformatica, TipoSala::LabEspecifico};
            } else if (podeLabEsp) {
                tipos = {TipoSala::LabEspecifico};
            } else if (podeLabInf) {
                tipos = {TipoSala::LabInformatica};
            }
        }

        if (tipos.empty()) tipos = tiposCategoria;
        return normalizarTipos(std::move(tipos));
    };

    auto filtrarPorTipos = [&](const std::vector<int>& dominio, const std::vector<TipoSala>& tipos) {
        std::vector<int> filtrado;
        for (int idx : dominio) {
            if (idx < 0 || idx >= static_cast<int>(salas.size())) continue;
            if (contemTipo(tipos, salas[idx].tipo)) filtrado.push_back(idx);
        }
        std::sort(filtrado.begin(), filtrado.end());
        filtrado.erase(std::unique(filtrado.begin(), filtrado.end()), filtrado.end());
        return filtrado;
    };

    for (auto& oc : ocorrencias) {
        const auto& turma = turmas[oc.idxTurma];
        auto it = mapa.find(turma.codigoUc);

        if (it != mapa.end()) {
            const auto& entry = it->second;
            std::vector<TipoSala> tiposCategoria = normalizarTipos(tiposDaCategoria(entry.categoria));
            if (tiposCategoria.empty()) {
                tiposCategoria = oc.tiposPermitidos;
                if (rel) {
                    rel->avisos.push_back("COD_UC=" + turma.codigoUc + " tem categoria desconhecida no mapeamento ('" + entry.categoria + "')");
                }
            }

            const auto dominioCompleto = resolverSalas(entry.salasPermitidas);
            oc.tiposPermitidos = escolherTiposDaOcorrencia(oc, tiposCategoria);
            oc.tipoSalaRequerido = oc.tiposPermitidos.empty()
                ? TipoSala::Outro
                : oc.tiposPermitidos.front();
            oc.salasPermitidas = filtrarPorTipos(dominioCompleto, oc.tiposPermitidos);

            if (oc.salasPermitidas.empty() && !dominioCompleto.empty() && rel) {
                rel->avisos.push_back("ocorrencia da turma " + std::to_string(turma.idTurma) + " (COD_UC=" + turma.codigoUc + ") ficou sem sala permitida apos filtrar por tipo fisico");
            }
        } else {
            // sem entrada no mapeamento: fallback pelo tipo do csv
            if (oc.tipoSalaRequerido == TipoSala::Sala) {
                oc.salasPermitidas = todasSalas;
                oc.tiposPermitidos = {TipoSala::Sala};
            } else if (oc.tipoSalaRequerido == TipoSala::LabInformatica) {
                oc.salasPermitidas = todosLabInf;
                oc.tiposPermitidos = {TipoSala::LabInformatica};
            } else if (oc.tipoSalaRequerido == TipoSala::LabEspecifico) {
                oc.salasPermitidas = todosLabEsp;
                oc.tiposPermitidos = {TipoSala::LabEspecifico};
                if (rel) {
                    rel->avisos.push_back("COD_UC=" + turma.codigoUc + " requer lab especifico sem entrada no mapeamento");
                }
            }
        }

        if (oc.salasPermitidas.empty() && rel) {
            rel->avisos.push_back("ocorrencia da turma " + std::to_string(turma.idTurma) + " (COD_UC=" + turma.codigoUc + ") ficou sem sala permitida");
        }
    }
}

void aplicarRestricoesAcessibilidade(std::vector<Sala>& salas, std::vector<Turma>& turmas, std::vector<Ocorrencia>& ocorrencias, RelatorioParse* rel) {
    const auto itSala210 = std::find_if(salas.begin(), salas.end(), [](const Sala& s) {
        return s.codigo == "210";
    });
    if (itSala210 == salas.end()) {
        if (rel) {
            rel->avisos.push_back("Regra PCD do docente Paiva nao aplicada: sala 210 inexistente");
        }
        return;
    }

    const int idxSala210 = itSala210->idx;
    salas[idxSala210].acessibilidade = true;

    int ocorrenciasFixadas = 0;
    for (auto& turma : turmas) {
        if (normalizar(turma.docente).find("paiva") == std::string::npos) continue;

        turma.acessibilidade = true;
        for (auto& oc : ocorrencias) {
            if (oc.idxTurma != turma.idx) continue;
            oc.tipoSalaRequerido = salas[idxSala210].tipo;
            oc.tiposPermitidos = {salas[idxSala210].tipo};
            oc.salasPermitidas = {idxSala210};
            ++ocorrenciasFixadas;
        }
    }

    if (rel && ocorrenciasFixadas > 0) {
        rel->avisos.push_back("Regra PCD: docente Paiva fixado na sala 210 em " + std::to_string(ocorrenciasFixadas) + " ocorrencias");
    }
}

void aplicarAcessibilidade(const std::vector<Sala>& salas, const std::vector<Turma>& turmas, std::vector<Ocorrencia>& ocorrencias, RelatorioParse* rel) {
    int ocorrenciasRestritas = 0;
    int turmasSemSalaAcessivel = 0;
    for (auto& oc : ocorrencias) {
        const auto& turma = turmas[oc.idxTurma];
        if (!turma.acessibilidade) continue;

        std::vector<int> filtrado;
        for (int idx : oc.salasPermitidas) {
            if (idx >= 0 && idx < static_cast<int>(salas.size()) && salas[idx].acessibilidade) {
                filtrado.push_back(idx);
            }
        }

        if (filtrado.empty()) {
            ++turmasSemSalaAcessivel;
            if (rel) {
                rel->avisos.push_back("Acessibilidade: turma " + std::to_string(turma.idTurma) +
                                      " exige acessibilidade mas nenhuma sala acessivel esta no dominio (sera inviabilidade)");
            }
            continue;
        }

        if (filtrado.size() != oc.salasPermitidas.size()) {
            oc.salasPermitidas = std::move(filtrado); // ja ordenado (subconjunto de dominio ordenado)
            ++ocorrenciasRestritas;
        }
    }

    if (rel && (ocorrenciasRestritas > 0 || turmasSemSalaAcessivel > 0)) {
        rel->avisos.push_back("Acessibilidade: dominio restrito em " + std::to_string(ocorrenciasRestritas) +
                              " ocorrencias; " + std::to_string(turmasSemSalaAcessivel) +
                              " ocorrencias sem sala acessivel no dominio");
    }
}

std::vector<std::vector<int>> carregarAdjacencias(const std::vector<std::string>& paths, const std::vector<Sala>& salas, RelatorioParse* rel) {
    constexpr int CUSTO_VERTICAL_POR_ANDAR = 1000;

    const int N = static_cast<int>(salas.size());
    std::vector<std::vector<int>> D(N, std::vector<int>(N, DIST_INF));
    std::vector<int> distElevador(N, DIST_INF);
    for (int i = 0; i < N; ++i) D[i][i] = 0;

    std::unordered_map<std::string, int> porCodigo;
    for (const auto& s : salas) porCodigo[s.codigo] = s.idx;

    auto parseDistancia = [&](const std::string& raw, const std::string& path, int& out) {
        const auto valor = trim(raw);
        if (valor.empty()) return false;
        try {
            out = static_cast<int>(std::lround(std::stod(valor)));
        } catch (...) {
            if (rel) {
                rel->avisos.push_back("distancia invalida em " + path + ": '" + valor + "'");
            }
            return false;
        }
        return out >= 0;
    };

    for (const auto& path : paths) {
        std::ifstream in(path);
        if (!in) {
            if (rel) rel->avisos.push_back("Falha ao abrir adjacencia: " + path);
            continue;
        }
        std::string header;
        if (!std::getline(in, header)) continue;
        header = removerBOM(header);
        const char delim = detectaDelimitador(header);

        const auto cab = splitCSV(header, delim);
        std::vector<int> colParaIdx(cab.size(), -1);
        int colElevador = -1;
        for (std::size_t i = 1; i < cab.size(); ++i) {
            const auto c = trim(cab[i]);
            if (normalizar(c) == "e") {
                colElevador = static_cast<int>(i);
                continue;
            }
            const auto it = porCodigo.find(c);
            if (it != porCodigo.end()) colParaIdx[i] = it->second;
        }

        std::string linha;
        while (std::getline(in, linha)) {
            linha = removerBOM(linha);
            if (trim(linha).empty()) continue;
            const auto campos = splitCSV(linha, delim);
            if (campos.empty()) continue;

            const auto codLinha = trim(campos[0]);
            const bool linhaElevador = normalizar(codLinha) == "e";
            const auto itL = porCodigo.find(codLinha);
            if (!linhaElevador && itL == porCodigo.end()) continue;
            const int i = linhaElevador ? -1 : itL->second;

            for (std::size_t j = 1; j < campos.size() && j < colParaIdx.size(); ++j) {
                int d = 0;
                if (!parseDistancia(campos[j], path, d)) continue;

                const int jj = colParaIdx[j];
                const bool colunaElevador = static_cast<int>(j) == colElevador;

                if (!linhaElevador && colunaElevador) {
                    distElevador[i] = std::min(distElevador[i], d);
                    continue;
                }

                if (linhaElevador && jj >= 0) {
                    distElevador[jj] = std::min(distElevador[jj], d);
                    continue;
                }

                if (i < 0 || jj < 0) continue;

                if (D[i][jj] < DIST_INF && D[i][jj] != d && rel) {
                    rel->adjacenciasInconsistentes++;
                }

                if (D[i][jj] >= DIST_INF || d < D[i][jj]) D[i][jj] = d;
                if (D[jj][i] >= DIST_INF || d < D[jj][i]) D[jj][i] = d;
            }
        }
    }

    for (int i = 0; i < N; ++i) {
        if (distElevador[i] >= DIST_INF || salas[i].andar < 0) continue;
        for (int j = i + 1; j < N; ++j) {
            if (distElevador[j] >= DIST_INF || salas[j].andar < 0) continue;
            if (salas[i].andar == salas[j].andar) continue;

            const int deltaAndar = salas[i].andar > salas[j].andar ? salas[i].andar - salas[j].andar : salas[j].andar - salas[i].andar;
            const int d = distElevador[i] + CUSTO_VERTICAL_POR_ANDAR * deltaAndar + distElevador[j];

            if (D[i][j] >= DIST_INF || d < D[i][j]) D[i][j] = d;
            if (D[j][i] >= DIST_INF || d < D[j][i]) D[j][i] = d;
        }
    }

    if (rel) {
        for (int i = 0; i < N; ++i) {
            if (distElevador[i] >= DIST_INF && salas[i].andar > 1) {
                rel->avisos.push_back("sala " + salas[i].codigo + " sem distancia ao elevador nas matrizes");
            }
        }

        int com = 0, sem = 0;
        for (int i = 0; i < N; ++i) {
            for (int j = 0; j < N; ++j) {
                if (i == j) continue;
                if (D[i][j] >= DIST_INF) ++sem;
                else ++com;
            }
        }
        rel->paresComDistancia = com;
        rel->paresSemDistancia = sem;
    }

    return D;
}

} // namespace

Instancia parse(const CaminhosCSV& caminhos, RelatorioParse* rel) {
    Instancia inst;
    inst.duracaoPadraoMin = caminhos.duracaoPadraoMin;

    inst.salas = parseSalas(caminhos.salas, rel);

    auto g = parseGrad(caminhos.grad, caminhos.duracaoPadraoMin, rel);
    inst.turmas = std::move(g.turmas);
    inst.ocorrencias = std::move(g.ocorrencias);

    auto mapa = parseMapeamento(caminhos.mapeamento, rel);
    aplicarMapeamento(inst.salas, inst.turmas, inst.ocorrencias, mapa, rel);
    aplicarRestricoesAcessibilidade(inst.salas, inst.turmas, inst.ocorrencias, rel);
    aplicarAcessibilidade(inst.salas, inst.turmas, inst.ocorrencias, rel);

    inst.distSalas = carregarAdjacencias(caminhos.adjacencias, inst.salas, rel);

    return inst;
}
