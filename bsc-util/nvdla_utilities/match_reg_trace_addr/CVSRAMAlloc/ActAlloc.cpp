#include <fstream>
#include <sstream>
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <string>
#include <algorithm>

#include <vector>
#include <cmath>

#include "gurobi_c++.h"

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))


struct AtomicTensor {
    uint64_t use_start;
    uint64_t use_end;
    uint64_t size;
    uint32_t num_access;
    uint32_t belong_to_tensor;

    AtomicTensor(uint32_t start, uint32_t end, uint32_t size, uint32_t accesses, uint32_t belong_to) :
            use_start(start), use_end(end), size(size), num_access(accesses), belong_to_tensor(belong_to) {}
};


struct TensorAllocInfo {
    uint64_t use_start;
    uint64_t use_end;
    uint64_t size;
    uint64_t dram_addr;
    uint32_t num_access;

    uint32_t line_stride_aligned;
    uint32_t surf_stride_aligned;
    int32_t hyp_tensor_id;
    std::vector<uint32_t> atomic_tensors;

    TensorAllocInfo(uint32_t start, uint32_t end, uint32_t size, uint32_t dram_addr, uint32_t line_stride,
                    uint32_t surf_stride, uint32_t accesses) :
        use_start(start), use_end(end), size(size), dram_addr(dram_addr), num_access(accesses),
        line_stride_aligned(line_stride), surf_stride_aligned(surf_stride), hyp_tensor_id(-1) {}

    bool is_strided() const {
        if (line_stride_aligned == 0) {
            return false;
        }
        uint32_t num_segment = (size - 1) / line_stride_aligned + 1;
        return surf_stride_aligned > line_stride_aligned && num_segment > 1;
    }

    void set_hyp_tensor_id(int32_t hyp) {
        hyp_tensor_id = hyp;
    }
};


struct HyperTensor {
    uint64_t bundled_size;
    std::vector<uint32_t> tensors;
    explicit HyperTensor(uint64_t _bundled_size) : bundled_size(_bundled_size) {}
};


class NoChangeTimeoutCallback: public GRBCallback {
public:
    time_t timeout_time;
    time_t abs_timeout_time;
    time_t base_sol_time;
    double epsilon;

    double last_gap;
    time_t last_time;
    time_t init_time;

    NoChangeTimeoutCallback(time_t _time = 60, time_t abs_timeout = 600, time_t base_time = 300, double _epsilon = 0.01):
        timeout_time(_time), abs_timeout_time(abs_timeout), base_sol_time(base_time), epsilon(_epsilon), last_gap(-1),
        last_time(time(nullptr)), init_time(time(nullptr)) {}

protected:
    void callback() override {
        if (where == GRB_CB_MIP) {
            double obj_bst = getDoubleInfo(GRB_CB_MIP_OBJBST);
            double obj_bnd = getDoubleInfo(GRB_CB_MIP_OBJBND);
            double this_gap = fabs(obj_bst - obj_bnd) / obj_bst;
            if (time(nullptr) - init_time > base_sol_time) {
                if (fabs(this_gap - last_gap) < epsilon * this_gap) {
                    if (time(nullptr) - last_time > timeout_time) {
                        std::cout << "gap has not changed for more than " << timeout_time << " seconds. Abort.\n";
                        abort();
                    }
                } else if (time(nullptr) - init_time > abs_timeout_time) {
                    std::cout << "has consumed over " << abs_timeout_time << " seconds, abort\n";
                    abort();
                } else {
                    last_gap = this_gap;
                    last_time = time(nullptr);
                }
            }
        }
    }
};


class AllocProblem {
private:
    char* out_file;
    uint64_t align;
    uint64_t mem_size;
    uint64_t M_size;    // possibly larger than mem_size
    uint64_t M_pos{0};  // possibly larger than mem_size
    uint64_t max_life_span;
    std::vector<AtomicTensor> atom_tensors;
    std::vector<TensorAllocInfo> tensors;
    std::vector<HyperTensor> hyp_tensors;
    std::vector<std::pair<uint32_t, uint32_t>> overlap;
public:
    AllocProblem(int argc, char** argv) {
        assert(argc >= 4);

        std::ifstream fin(argv[1], std::ios::in);
        out_file = argv[2];

        std::string line;
        M_size = 0;

        max_life_span = 0;

        // get tensor information
        while (std::getline(fin, line)) {
            if (line.find("-----") != std::string::npos) {
                break;
            }
            std::stringstream line_stream(line);
            uint64_t start, end, accesses;
            uint64_t size, dram_addr, orig_line_stride, orig_surf_stride;
            line_stream >> std::dec >> start >> end >> size >> orig_line_stride >> orig_surf_stride >> accesses >> std::hex >> dram_addr;
            max_life_span = max_life_span > (end - start) ? max_life_span : (end - start);

            tensors.emplace_back(start, end, size, dram_addr, orig_line_stride, orig_surf_stride, accesses);
        }

        while (std::getline(fin, line)) {
            std::stringstream line_stream(line);
            if (line.size() < 2) { // an empty line
                continue;
            }

            // here each line represents a hyper-tensor
            uint64_t bundled_size;
            line_stream >> std::dec >> bundled_size;
            hyp_tensors.emplace_back(bundled_size);
            auto& hyp_tensor = hyp_tensors.back();
            uint32_t tensor_id;
            while (line_stream >> std::dec >> tensor_id) {
                hyp_tensor.tensors.emplace_back(tensor_id);
                tensors[tensor_id].set_hyp_tensor_id(hyp_tensors.size() - 1);
            }
            std::sort(hyp_tensor.tensors.begin(), hyp_tensor.tensors.end(),
                      [&](auto a, auto b) {return tensors[a].dram_addr < tensors[b].dram_addr;});
        }

        // construct atomic_tensors using tensors
        for (size_t id = 0; id < tensors.size(); id++) {
            auto& tensor = tensors[id];
            if (tensor.is_strided()) {
                uint32_t num_segment = (tensor.size - 1) / tensor.line_stride_aligned + 1;
                assert(num_segment > 1);
                for (uint32_t i = 0; i < num_segment; i++) {
                    atom_tensors.emplace_back(tensor.use_start, tensor.use_end, tensor.line_stride_aligned, tensor.num_access, id);
                    tensor.atomic_tensors.emplace_back(atom_tensors.size() - 1);
                    // std::cout << "Atomic Tensor emplaced: " << atom_tensors.size() - 1 << ", size: " << tensor.line_stride_aligned <<"\n";
                }
            } else {
                atom_tensors.emplace_back(tensor.use_start, tensor.use_end, tensor.size, tensor.num_access, id);
                tensor.atomic_tensors.emplace_back(atom_tensors.size() - 1);
            }
        }

        // get overlapped atomic tensors
        for (uint32_t i = 0; i < atom_tensors.size(); i++)
            for (uint32_t j = i + 1; j < atom_tensors.size(); j++)
                if (min(atom_tensors[i].use_end, atom_tensors[j].use_end) > max(atom_tensors[i].use_start, atom_tensors[j].use_start))
                    overlap.emplace_back(i, j);

        mem_size = atoi(argv[3]);

        // First, get the log2 value of the first atomic tensor
        uint64_t log_divider = 1;
        while (true) {
            if ((atom_tensors[0].size >> log_divider) << log_divider == atom_tensors[0].size) {
                log_divider++;
            } else {
                break;
            }
        }
        log_divider--;

        // Second, sweep through all the atomic tensors
        for (auto& atom_tensor: atom_tensors) {
            uint64_t this_log_divider = 1;
            while (true) {
                if ((atom_tensor.size >> this_log_divider) << this_log_divider == atom_tensor.size) {
                    this_log_divider++;
                } else {
                    break;
                }
            }
            this_log_divider--;
            log_divider = log_divider < this_log_divider ? log_divider : this_log_divider;
        }

        // Third, sweep through the difference between DRAM addresses of tensors of the same hyper tensor
        for (auto& hyper_tensor: hyp_tensors) {
            for (size_t tensor_id = 0; tensor_id < hyper_tensor.tensors.size() - 1; tensor_id++) {
                int64_t dram_addr_diff = tensors[hyper_tensor.tensors[tensor_id]].dram_addr - tensors[hyper_tensor.tensors[tensor_id + 1]].dram_addr;
                dram_addr_diff = dram_addr_diff < 0 ? -dram_addr_diff : dram_addr_diff;
                uint64_t this_log_divider = 1;
                while (true) {
                    if ((dram_addr_diff >> this_log_divider) << this_log_divider == dram_addr_diff) {
                        this_log_divider++;
                    } else {
                        break;
                    }
                }
                this_log_divider--;
                log_divider = log_divider < this_log_divider ? log_divider : this_log_divider;
            }
        }
        align = (1 << log_divider);

        for (auto& tensor: tensors) {
            tensor.size = (tensor.size - 1) / align + 1;
            tensor.dram_addr = (tensor.dram_addr - 1) / align + 1;
            tensor.line_stride_aligned = (tensor.line_stride_aligned - 1) / align + 1;
            tensor.surf_stride_aligned = (tensor.surf_stride_aligned - 1) / align + 1;
            M_size = tensor.size > M_size ? tensor.size : M_size;
        }

        for (auto& atomic_tensor: atom_tensors) {
            atomic_tensor.size = (atomic_tensor.size - 1) / align + 1;
            M_size = atomic_tensor.size > M_size ? atomic_tensor.size : M_size;
        }

        for (auto& hyper_tensor: hyp_tensors) {
            hyper_tensor.bundled_size = (hyper_tensor.bundled_size - 1) / align + 1;
            M_size = hyper_tensor.bundled_size > M_size ? hyper_tensor.bundled_size : M_size;
            M_pos = hyper_tensor.bundled_size > M_pos ? hyper_tensor.bundled_size : M_pos;
        }

        assert((mem_size / align) * align == mem_size);
        mem_size /= align;
        M_pos = M_pos > mem_size ? M_pos : mem_size;
        std::cout << "M_pos = " << M_pos << "; M_size = " << M_size << "\n";
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
            std::vector<GRBVar> pos; pos.reserve(atom_tensors.size());
            std::vector<GRBVar> sel; sel.reserve(atom_tensors.size());
            std::vector<GRBVar> sij; sij.reserve(overlap.size());
            std::vector<GRBVar> bij; bij.reserve(overlap.size());
            for (uint32_t i = 0; i < atom_tensors.size(); i++) {
                pos.emplace_back(model.addVar(0, M_pos - 1, 0, GRB_INTEGER, std::string("pos") + std::to_string(i)));
                sel.emplace_back(model.addVar(0, 1, 0, GRB_BINARY, std::string("sel") + std::to_string(i)));
            }

            for (uint32_t i = 0; i < overlap.size(); i++) {
                sij.emplace_back(model.addVar(0, 1, 0, GRB_BINARY, std::string("sij") + std::to_string(i)));
                bij.emplace_back(model.addVar(0, 1, 0, GRB_BINARY, std::string("bij") + std::to_string(i)));
            }

            GRBLinExpr obj = 0;
            for (uint32_t i = 0; i < atom_tensors.size(); i++) {
                obj += sel[i] * atom_tensors[i].size * atom_tensors[i].num_access / M_size;
                obj -= 0.01 * pos[i] / M_pos;
//                obj -= 0.001 * (sel[i] * (atom_tensors[i].use_end - atom_tensors[i].use_start) * atom_tensors[i].size *
//                    (pos[i] + 0.5 * atom_tensors[i].size)) / (max_life_span * M_size * (M_pos + 0.5 * M_size));
            }

            model.setObjective(obj, GRB_MAXIMIZE);

            // cannot exceed bound
            for (uint32_t i = 0; i < atom_tensors.size(); i++) {
                model.addConstr(pos[i] + atom_tensors[i].size - (M_size + M_pos) * (1 - sel[i]) <= mem_size);
                // std::cout << "pos[" << i << "] + " << atom_tensors[i].size << " - " << (M_size + M_pos) << " * (1 - sel[" << i << "]) <= " << mem_size << "\n";
            }
            // std::cout << "\n";
            // either i is beneath j
            for (uint32_t i = 0; i < overlap.size(); i++) {
                uint32_t first = overlap[i].first;
                model.addConstr(pos[first] + atom_tensors[first].size - M_pos * bij[i] -
                                M_size * (1 - sij[i]) <= pos[overlap[i].second]);
                // std::cout << "pos[" << first << "] + " << atom_tensors[first].size << " - " << M_pos << " * bij[" << i << "] - " << M_size << " * (1 - " << "sij[" << i << "]) <= pos[" << overlap[i].second << "]\n";
            }

            // or j is beneath i
            for (uint32_t i = 0; i < overlap.size(); i++) {
                uint32_t second = overlap[i].second;
                model.addConstr(pos[second] + atom_tensors[second].size - M_pos * (1 - bij[i]) -
                                M_size * (1 - sij[i]) <= pos[overlap[i].first]);
            }

            // sij = sel[i] * sel[j]
            for (uint32_t i = 0; i < overlap.size(); i++) {
                model.addConstr(sij[i] >= sel[overlap[i].first] + sel[overlap[i].second] - 1);
                model.addConstr(sij[i] <= sel[overlap[i].first]);
                model.addConstr(sij[i] <= sel[overlap[i].second]);
            }

            // for atomic tensors from the same tensor:
            for (auto& tensor: tensors) {
                if (tensor.atomic_tensors.size() > 1) {
                    // require either all of them are selected, or none are selected
                    for (uint32_t i = 1; i < tensor.atomic_tensors.size(); i++) {
                        model.addConstr(sel[tensor.atomic_tensors[0]] == sel[tensor.atomic_tensors[i]]);
                        // std::cout << "sel[" << tensor.atomic_tensors[0] << "] == sel[" << tensor.atomic_tensors[i] << "]\n";
                    }

                    // require they are positioned according to surf_stride
                    for (uint32_t i = 1; i < tensor.atomic_tensors.size(); i++) {
                        model.addConstr(pos[tensor.atomic_tensors[0]] + i * tensor.surf_stride_aligned == pos[tensor.atomic_tensors[i]]);
                        // std::cout << "pos[" << tensor.atomic_tensors[0] << "] + " << i << " * " << tensor.surf_stride_aligned << " == pos[" << tensor.atomic_tensors[i] << "]\n";
                    }
                }
            }

            // for tensors from the same hyper_tensor
            for (auto& hyp_tensor : hyp_tensors) {
                for (uint32_t i = 1; i < hyp_tensor.tensors.size(); i++) {
                    uint32_t tensor_id = hyp_tensor.tensors[i];

                    // require either all of its tensors are selected, or none are selected
                    model.addConstr(sel[tensors[hyp_tensor.tensors[0]].atomic_tensors[0]] ==
                                    sel[tensors[tensor_id].atomic_tensors[0]]);
                    // std::cout << std::dec << "sel[" << tensors[hyp_tensor.tensors[0]].atomic_tensors[0] << "] == sel[" << tensors[tensor_id].atomic_tensors[0] << "]\n";

                    // require the relative positions of tensors in the same hyper-tensor to be the same with original
                    model.addConstr(pos[tensors[tensor_id].atomic_tensors[0]] -
                                    pos[tensors[hyp_tensor.tensors[0]].atomic_tensors[0]] ==
                                    tensors[tensor_id].dram_addr - tensors[hyp_tensor.tensors[0]].dram_addr);
                    // std::cout << "pos[" << tensors[tensor_id].atomic_tensors[0] << "] - pos[" << tensors[hyp_tensor.tensors[0]].atomic_tensors[0] << "] == 0x" << std::hex << tensors[tensor_id].dram_addr << " - 0x" << tensors[hyp_tensor.tensors[0]].dram_addr << "\n";
                }
            }

            // Optimize model
            model.optimize();

            // collect solution
            std::ofstream fout(out_file, std::ios::out);
            for (auto& tensor: tensors) {
                auto main_atom_id = tensor.atomic_tensors[0];
                fout << int(sel[main_atom_id].get(GRB_DoubleAttr_X)) << " " << int(pos[main_atom_id].get(GRB_DoubleAttr_X)) * align << "\n";
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