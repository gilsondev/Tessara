#include "weather/codes.h"

namespace weather {

const char *phrase(int code) {
    switch (code) {
        case 0: return "céu limpo";
        case 1: return "quase limpo";
        case 2: return "parcial";
        case 3: return "nublado";
        case 45:
        case 48: return "neblina";
        case 51:
        case 53:
        case 55:
        case 56:
        case 57: return "garoa";
        case 61:
        case 63:
        case 65:
        case 66:
        case 67: return "chuva";
        case 71:
        case 73:
        case 75:
        case 77: return "neve";
        case 80:
        case 81:
        case 82:
        case 85:
        case 86: return "pancadas";
        case 95:
        case 96:
        case 99: return "tempestade";
        default: return "tempo";
    }
}

}  // namespace weather
