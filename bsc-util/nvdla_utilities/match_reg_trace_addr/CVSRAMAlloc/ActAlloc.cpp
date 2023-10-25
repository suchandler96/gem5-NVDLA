#include <fstream>
#include <sstream>
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <string>

#include <vector>
#include <cmath>

#include "gurobi_c++.h"

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))


struct TensorAllocInfo {
    uint32_t use_start;
    uint32_t use_end;
    uint32_t size;
    uint32_t num_access;

    TensorAllocInfo(uint32_t start, uint32_t end, uint32_t size, uint32_t accesses) :
        use_start(start), use_end(end), size(size), num_access(accesses) {}
};


class NoChangeTimeoutCallback: public GRBCallback {
public:
    time_t timeout_time;
    double epsilon;

    double last_gap;
    time_t last_time;

    explicit NoChangeTimeoutCallback(time_t _time = 45, double _epsilon = 0.01):
            timeout_time(_time), epsilon(_epsilon), last_gap(-1), last_time(time(nullptr)) {}

protected:
    void callback() override {
        if (where == GRB_CB_MIP) {
            double obj_bst = getDoubleInfo(GRB_CB_MIP_OBJBST);
            double obj_bnd = getDoubleInfo(GRB_CB_MIP_OBJBND);
            double this_gap = fabs(obj_bst - obj_bnd) / obj_bst;
            if (fabs(this_gap - last_gap) < epsilon * this_gap) {
                if (time(nullptr) - last_time > timeout_time) {
                    std::cout << "gap has not changed for more than " << timeout_time << " seconds. Abort.\n";
                    abort();
                }
            } else {
                last_gap = this_gap;
                last_time = time(nullptr);
            }
        }
    }
};


class AllocProblem {
private:
    char* out_file;
    uint32_t align;
    uint32_t mem_size;
    uint32_t max_tensor_size;   // possibly larger than mem_size
    std::vector<TensorAllocInfo> tensors;
    std::vector<std::pair<uint32_t, uint32_t>> overlap;
public:
    AllocProblem(int argc, char** argv) {
        assert(argc >= 4);
        align = (argc >= 5) ? atoi(argv[4]) : 0x1000;

        std::ifstream fin(argv[1], std::ios::in);
        out_file = argv[2];

        std::string line;
        max_tensor_size = 0;
        while(std::getline(fin, line)) {
            std::stringstream line_stream(line);
            uint32_t start, end, accesses;
            uint64_t size;
            line_stream >> start >> end >> size >> accesses;
            size = (size - 1) / align + 1;
            max_tensor_size = size > max_tensor_size ? size : max_tensor_size;

            tensors.emplace_back(start, end, size, accesses);
        }

        for (uint32_t i = 0; i < tensors.size(); i++)
            for (uint32_t j = i + 1; j < tensors.size(); j++)
                if (min(tensors[i].use_end, tensors[j].use_end) > max(tensors[i].use_start, tensors[j].use_start))
                    overlap.emplace_back(i, j);

        mem_size = atoi(argv[3]);
        assert((mem_size / align) * align == mem_size);
        mem_size /= align;
    }

    void FormulateILP() {
        try {
            // Create an environment
            GRBEnv env = GRBEnv(true);
            env.set("LogFile", "mip1.log");
            // env.set(GRB_IntParam_Threads, 1);
            env.start();

            // Create an empty model
            GRBModel model = GRBModel(env);
            auto cb = NoChangeTimeoutCallback();

            model.setCallback(&cb);

            // Create variables
            std::vector<GRBVar> pos; pos.reserve(tensors.size());
            std::vector<GRBVar> sel; sel.reserve(tensors.size());
            std::vector<GRBVar> sij; sij.reserve(overlap.size());
            std::vector<GRBVar> bij; bij.reserve(overlap.size());
            for (uint32_t i = 0; i < tensors.size(); i++) {
                pos.emplace_back(model.addVar(0, mem_size - 1, 0, GRB_INTEGER, std::string("pos") + std::to_string(i)));
                sel.emplace_back(model.addVar(0, 1, 0, GRB_BINARY, std::string("sel") + std::to_string(i)));
            }

            for (uint32_t i = 0; i < overlap.size(); i++) {
                sij.emplace_back(model.addVar(0, 1, 0, GRB_BINARY, std::string("sij") + std::to_string(i)));
                bij.emplace_back(model.addVar(0, 1, 0, GRB_BINARY, std::string("bij") + std::to_string(i)));
            }

            GRBLinExpr obj = 0;
            for (uint32_t i = 0; i < tensors.size(); i++)
                obj += sel[i] * tensors[i].size * tensors[i].num_access;

            model.setObjective(obj, GRB_MAXIMIZE);

            // cannot exceed bound
            for (uint32_t i = 0; i < tensors.size(); i++)
                model.addConstr(pos[i] + tensors[i].size - max_tensor_size * (1 - sel[i]) <= mem_size);

            // either i is beneath j
            for (uint32_t i = 0; i < overlap.size(); i++) {
                uint32_t first = overlap[i].first;
                model.addConstr(pos[first] + tensors[first].size - mem_size * bij[i] - max_tensor_size * (1 - sij[i]) <= pos[overlap[i].second]);
            }

            // or j is beneath i
            for (uint32_t i = 0; i < overlap.size(); i++) {
                uint32_t second = overlap[i].second;
                model.addConstr(pos[second] + tensors[second].size - mem_size * (1 - bij[i]) - max_tensor_size * (1 - sij[i]) <= pos[overlap[i].first]);
            }

            // sij = sel[i] * sel[j]
            for (uint32_t i = 0; i < overlap.size(); i++) {
                model.addConstr(sij[i] >= sel[overlap[i].first] + sel[overlap[i].second] - 1);
                model.addConstr(sij[i] <= sel[overlap[i].first]);
                model.addConstr(sij[i] <= sel[overlap[i].second]);
            }

            // Optimize model
            model.optimize();

            std::ofstream fout(out_file, std::ios::out);
            for (uint32_t i = 0; i < sel.size(); i++) {
                fout << int(sel[i].get(GRB_DoubleAttr_X)) << " " << int(pos[i].get(GRB_DoubleAttr_X)) * align << "\n";
            }

            std::cout << "Obj: " << model.get(GRB_DoubleAttr_ObjVal) << std::endl;

          } catch(GRBException e) {
            std::cout << "Error code = " << e.getErrorCode() << std::endl;
            std::cout << e.getMessage() << std::endl;
          } catch(...) {
            std::cout << "Exception during optimization" << std::endl;
          }
    }
};


int main(int argc, char** argv) {
    auto alloc_problem = AllocProblem(argc, argv);
    alloc_problem.FormulateILP();


    return 0;
}