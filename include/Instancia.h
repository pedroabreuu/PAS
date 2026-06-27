#pragma once

#include <climits>
#include <string>
#include <vector>

constexpr int DIST_INF = INT_MAX / 4;

enum class DiaSemana {
    Segunda, Terca, Quarta, Quinta, Sexta, Sabado
};

enum class TipoSala {
    Sala,
    LabInformatica,
    LabEspecifico,
    Outro
};

struct Horario {
    int inicio = 0;
    int fim = 0;
};

struct Sala {
    int idx = -1;
    std::string codigo;
    std::string nomeOriginal;
    std::string unidade;
    int andar = -1;
    int capacidade = 0;
    TipoSala tipo = TipoSala::Sala;
    bool acessibilidade = false; // sala possui acessibilidade ou nao
    bool disponivel = true;
};

struct Turma {
    int idx = -1;
    int idTurma = -1;
    std::string codigoUc;
    std::string disciplina;
    std::string subgrupo;
    int termo = 0;
    int vagas = 0;
    int inscritos = 0;
    std::string docente;
    std::string departamento;

    bool acessibilidade = false; // turma necessita de acessibilidade
};

struct Ocorrencia {
    int idxTurma = -1;
    DiaSemana diaSemana = DiaSemana::Segunda;
    Horario horario;
    TipoSala tipoSalaRequerido = TipoSala::Sala;
    std::string tipoSalaOriginal;
    std::vector<TipoSala> tiposPermitidos;
    std::vector<int> salasPermitidas; // dominio explicito indices das salas permitidas vem do mapeamento.csv
};

struct Instancia {
    std::vector<Sala> salas;
    std::vector<Turma> turmas;
    std::vector<Ocorrencia> ocorrencias;
    std::vector<std::vector<int>> distSalas;
    int duracaoPadraoMin = 120; // duracao das aulas em min
};
