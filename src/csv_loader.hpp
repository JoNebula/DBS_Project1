#pragma once
#include "record.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

inline std::vector<StudentRecord> load_csv(const std::string& path) {
    std::ifstream fin(path);
    if (!fin) throw std::runtime_error("Cannot open CSV: " + path);

    std::vector<StudentRecord> records;
    records.reserve(100001);

    std::string line;
    std::getline(fin, line);  // header

    while (std::getline(fin, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string id_s, name, gender, gpa_s, h_s, w_s;
        std::getline(ss, id_s,    ',');
        std::getline(ss, name,    ',');
        std::getline(ss, gender,  ',');
        std::getline(ss, gpa_s,   ',');
        std::getline(ss, h_s,     ',');
        std::getline(ss, w_s,     ',');

        StudentRecord r;
        r.student_id = std::stoll(id_s);
        r.name       = name;
        r.gender     = gender;
        r.gpa        = std::stod(gpa_s);
        r.height     = std::stod(h_s);
        r.weight     = std::stod(w_s);
        records.push_back(std::move(r));
    }
    return records;
}
