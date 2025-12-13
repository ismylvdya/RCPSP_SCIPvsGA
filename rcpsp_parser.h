#pragma once
#include <vector>
#include <string>
#include <unordered_map>

struct Task {
    int id;
    int duration;
    std::vector<int> successors;
    std::vector<int> resources;
    std::vector<int> predecessors;
};

struct Instance {
    int n_jobs;
    int n_resources;
    std::vector<Task> tasks;
};

struct Resource {
    int capacity;
};

struct RCPSPInstance {
    int n_jobs;
    int n_resources;
    std::vector<Task> tasks;
    std::vector<Resource> resources;
};

// Парсинг PSPLIB-файла (.sm)
RCPSPInstance parse_sm_file(const std::string& filepath);
