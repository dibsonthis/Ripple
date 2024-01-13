#include "utils.hpp"

bool vector_contains_string(std::vector<std::string>& vec, std::string value) {
    return std::find(vec.begin(), vec.end(), value) != vec.end();
}

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if(from.empty()) {
        return;
    }
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}