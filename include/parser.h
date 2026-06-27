#pragma once

#include <string>
#include <vector>
#include "Instancia.h"

struct CaminhosCSV {
    std::string salas;
    std::string grad;
    std::string mapeamento;
    std::vector<std::string> adjacencias;
    int duracaoPadraoMin = 120;
};

struct RelatorioParse {
    int salasLidas = 0;
    int salasIgnoradas = 0;
    int labsEspecificosDesativados = 0;

    int turmasLidas = 0;
    int turmasComInconsistencia = 0;
    int ocorrenciasLidas = 0;
    int ocorrenciasComTipoVazio = 0;

    int paresComDistancia = 0;
    int paresSemDistancia = 0;
    int adjacenciasInconsistentes = 0;

    std::vector<std::string> avisos;
};

Instancia parse(const CaminhosCSV& caminhos, RelatorioParse* relatorio = nullptr);