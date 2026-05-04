#pragma once
#include <cstdint>
#include <string>

using Key = int64_t;
using RID = int64_t;  // index into the in-memory record array

struct StudentRecord {
    Key student_id;
    std::string name;
    std::string gender;   // "Male" / "Female"
    double gpa;
    double height;
    double weight;
};
