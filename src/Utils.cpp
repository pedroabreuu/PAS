#include "Utils.h"
#include <cctype>
#include <stdexcept>

std::string trim(const std::string& s) {
    const std::string ws = " \t\r\n\"";
    const auto first = s.find_first_not_of(ws);
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

std::string removerBOM(const std::string& s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
        return s.substr(3);
    }
    return s;
}

static int proximoCodepoint(const std::string& s, std::size_t& pos) {
    if (pos >= s.size()) return -1;
    const auto c = static_cast<unsigned char>(s[pos]);
    int cp = -1;
    std::size_t n = 0;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
    else { pos += 1; return -1; }

    if (pos + n > s.size()) { pos = s.size(); return -1; }
    for (std::size_t k = 1; k < n; ++k) {
        const auto cc = static_cast<unsigned char>(s[pos + k]);
        if ((cc & 0xC0) != 0x80) { pos += 1; return -1; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    pos += n;
    return cp;
}

static std::string stripAcentosLower(int cp) {
    if (cp < 0) return " ";
    if (cp == 0x00AD || cp == 0x200B || cp == 0xFEFF) return "";
    if (cp >= 'A' && cp <= 'Z') return std::string(1, static_cast<char>(cp - 'A' + 'a'));
    if (cp >= 'a' && cp <= 'z') return std::string(1, static_cast<char>(cp));
    if (cp >= '0' && cp <= '9') return std::string(1, static_cast<char>(cp));
    switch (cp) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5: return "a";
        case 0x00C7: case 0x00E7: return "c";
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: return "e";
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: return "i";
        case 0x00D1: case 0x00F1: return "n";
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6:
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: return "o";
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: return "u";
        case 0x00DD: case 0x00FD: case 0x00FF: return "y";
        case 0x00BA: case 0x00B0: return " ";
        default: return " ";
    }
}

std::string normalizar(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) out += stripAcentosLower(proximoCodepoint(s, i));

    std::string r;
    r.reserve(out.size());
    bool prevSpace = true;
    for (char c : out) {
        if (c == ' ') {
            if (!prevSpace) {
                r.push_back(' ');
                prevSpace = true;
            }
        } else {
            r.push_back(c);
            prevSpace = false;
        }
    }
    if (!r.empty() && r.back() == ' ') r.pop_back();
    return r;
}

std::vector<std::string> splitCSV(const std::string& linha, char delim) {
    std::vector<std::string> campos;
    std::string atual;
    bool inQuotes = false;
    for (std::size_t i = 0; i < linha.size(); ++i) {
        const char c = linha[i];
        if (c == '"') {
            if (inQuotes && i + 1 < linha.size() && linha[i + 1] == '"') {
                atual.push_back('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == delim && !inQuotes) {
            campos.push_back(atual);
            atual.clear();
        } else {
            atual.push_back(c);
        }
    }
    campos.push_back(atual);
    return campos;
}

char detectaDelimitador(const std::string& linha) {
    int ponto_v = 0, virg = 0, tab = 0;
    bool inQuotes = false;
    for (char c : linha) {
        if (c == '"') { inQuotes = !inQuotes; continue; }
        if (inQuotes) continue;
        if (c == ';') ++ponto_v;
        else if (c == ',') ++virg;
        else if (c == '\t') ++tab;
    }
    if (tab >= ponto_v && tab >= virg && tab > 0) return '\t';
    if (ponto_v >= virg && ponto_v > 0) return ';';
    if (virg > 0) return ',';
    return ',';
}

int parseHorario(const std::string& s) {
    const auto t = trim(s);
    const auto p = t.find(':');
    if (p == std::string::npos) throw std::runtime_error("Horario invalido: '" + s + "'");
    const int h = std::stoi(t.substr(0, p));
    const int m = std::stoi(t.substr(p + 1));
    if (h < 0 || h > 23 || m < 0 || m > 59) {
        throw std::runtime_error("Horario invalido: '" + s + "'");
    }
    return h * 60 + m;
}

DiaSemana parseDia(const std::string& s) {
    const auto n = normalizar(s);
    if (n == "seg" || n == "segunda" || n == "segunda feira") return DiaSemana::Segunda;
    if (n == "ter" || n == "terca" || n == "terca feira") return DiaSemana::Terca;
    if (n == "qua" || n == "quarta" || n == "quarta feira") return DiaSemana::Quarta;
    if (n == "qui" || n == "quinta" || n == "quinta feira") return DiaSemana::Quinta;
    if (n == "sex" || n == "sexta" || n == "sexta feira") return DiaSemana::Sexta;
    if (n == "sab" || n == "sabado") return DiaSemana::Sabado;
    throw std::runtime_error("Dia invalido: '" + s + "'");
}

std::string nomeDia(DiaSemana d) {
    switch (d) {
        case DiaSemana::Segunda: return "seg";
        case DiaSemana::Terca:   return "ter";
        case DiaSemana::Quarta:  return "qua";
        case DiaSemana::Quinta:  return "qui";
        case DiaSemana::Sexta:   return "sex";
        case DiaSemana::Sabado:  return "sab";
    }
    return "?";
}

int parseTermo(const std::string& s) {
    const auto n = normalizar(s);
    for (char c : n) {
        if (std::isdigit(static_cast<unsigned char>(c))) return c - '0';
    }
    return 0;
}
