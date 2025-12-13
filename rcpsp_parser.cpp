#include "rcpsp_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>

RCPSPInstance parse_sm_file(const std::string& filepath) {
    std::ifstream fin(filepath);
    if (!fin.is_open())
        throw std::runtime_error("Cannot open " + filepath);

    RCPSPInstance inst;
    std::string line;
    bool in_precedence = false;
    bool in_requests = false;
    bool in_resources = false;

    std::unordered_map<int, std::vector<int>> successors;
    std::unordered_map<int, int> durations;
    std::unordered_map<int, std::vector<int>> resource_usage;
    std::vector<int> capacities;

    while (std::getline(fin, line)) {
        if (line.find("PRECEDENCE RELATIONS") != std::string::npos)
            in_precedence = true;
        else if (line.find("REQUESTS/DURATIONS") != std::string::npos) {
            in_precedence = false;
            in_requests = true;
        } else if (line.find("RESOURCEAVAILABILITIES") != std::string::npos) {
            in_requests = false;
            in_resources = true;
        } else if (line.find("************************************************************************") != std::string::npos) {
            in_precedence = in_requests = in_resources = false;
        } else if (in_precedence && line.find("jobnr.") == std::string::npos && !line.empty() && line.find("----") == std::string::npos) {
            std::istringstream iss(line);
            int job, modes, nsucc;
            iss >> job >> modes >> nsucc;
            std::vector<int> succ(nsucc);
            for (int i = 0; i < nsucc; ++i) iss >> succ[i];
            successors[job] = succ;
        } else if (in_requests && line.find("jobnr.") == std::string::npos && !line.empty() && line.find("---") == std::string::npos) {
            std::istringstream iss(line);
            int job, mode, dur;
            iss >> job >> mode >> dur;
            std::vector<int> res;
            int r;
            while (iss >> r) res.push_back(r);
            durations[job] = dur;
            resource_usage[job] = res;
        } else if (in_resources && !line.empty() && line.find("R ") == std::string::npos) {
            std::istringstream iss(line);
            int c;
            while (iss >> c) capacities.push_back(c);
        }
    }

    inst.n_resources = (int)capacities.size();
    for (int cap : capacities) inst.resources.push_back({cap});
    inst.n_jobs = (int)durations.size();

    for (auto& [id, dur] : durations) {
        Task t;
        t.id = id;
        t.duration = dur;
        t.successors = successors[id];
        t.resources = resource_usage[id];
        inst.tasks.push_back(t);
    }

    // очистка мусорных индексов
    for (auto &task : inst.tasks) {
        std::vector<int> valid_succ;
        for (int s : task.successors) {
            if (s > 0 && s <= inst.n_jobs) {
                valid_succ.push_back(s);
            }
        }
        task.successors = valid_succ;
    }

    for (auto &task : inst.tasks) {
        task.predecessors.clear();
    }
    for (auto &task : inst.tasks) {
        for (int succ : task.successors) {
            inst.tasks[succ - 1].predecessors.push_back(task.id);
        }
    }

    return inst;
}
