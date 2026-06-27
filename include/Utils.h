#pragma once

#include <string>
#include <vector>
#include "Instancia.h"

std::string trim(const std::string& s);
std::string removerBOM(const std::string& s);
std::string normalizar(const std::string& s);
std::vector<std::string> splitCSV(const std::string& linha, char delim);
char detectaDelimitador(const std::string& linha);

int parseHorario(const std::string& s);
DiaSemana parseDia(const std::string& s);
std::string nomeDia(DiaSemana d);
int parseTermo(const std::string& s);