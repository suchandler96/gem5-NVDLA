#include "embeddedBuffer.hh"


abstractSet::abstractSet(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _assoc) :
        wrapper(wrap), spm_latency(_lat), spm_line_size(_line_size), assoc(_assoc) {
    addr_map.reserve(assoc);
}


abstractSet::~abstractSet() = default;


allBufferSet::allBufferSet(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _assoc) :
        abstractSet(wrap, _lat, _line_size, _assoc) {
    printf("allBufferSet(%#lx, %u, %u, %u)\n", (uint64_t)wrap, _lat, _line_size, _assoc);
    for (uint32_t i = 0; i < assoc; i++) {
        lru_order.emplace_back(i);
    }
    printf("lru_order.size() = %lu\n", lru_order.size());

    lines.reserve(assoc);
    for (auto it = lru_order.begin(); it != lru_order.end(); it++) {
        lines.emplace_back(spm_line_size, it, addr_map.end());
    }
    printf("lines.size() = %lu\n", lines.size());
}


bool allBufferSet::read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out) {
    assert((axi_addr & (uint64_t)(AXI_WIDTH / 8 - 1)) == 0);

    uint64_t addr_base = axi_addr & ~(uint64_t)(spm_line_size - 1);
    uint64_t offset = axi_addr & (uint64_t)(spm_line_size - 1);
    printf("allBufferSet::read_spm_axi_line(%#lx, %#lx)\n", axi_addr, (uint64_t)data_out);

    auto addr_map_it = addr_map.find(addr_base);
    if (addr_map_it == addr_map.end())
        return false;

    auto& entry = lines[addr_map_it->second];
#ifndef NO_DATA
    for (int i = 0; i < AXI_WIDTH / 8; i++)
        data_out[i] = entry.spm_line[offset + i];
#endif
    lru_order.splice(lru_order.end(), lru_order, entry.lru_it);
    return true;
}


void allBufferSet::write_spm_axi_line_with_mask(uint64_t axi_addr, const uint8_t* data, uint64_t mask) {
    assert((axi_addr & (uint64_t)(AXI_WIDTH / 8 - 1)) == 0);

    uint64_t addr_base = axi_addr & ~(uint64_t)(spm_line_size - 1);
    uint64_t offset = axi_addr & (uint64_t)(spm_line_size - 1);
    printf("allBufferSet::write_spm_axi_line_with_mask(%#lx, %#lx, %#lx)\n", axi_addr, (uint64_t)data, mask);

    auto addr_map_it = addr_map.find(addr_base);
    if (addr_map_it == addr_map.end()) {
        printf("\tnot found, allocate\n");
        uint32_t to_write_vec_id = assoc;
        if (addr_map.size() >= assoc) {
            printf("\tcurrent addr_map.size() = %lu\n", addr_map.size());
            to_write_vec_id = erase_victim();       // lru_order maintenance of erasing is done inside
            printf("\tafter erase addr_map.size() = %lu\n", addr_map.size());
            printf("\terase victim id %u\n", to_write_vec_id);
        } else {
            // find a currently invalid entry to write in
            for (uint32_t i = 0; i < lines.size(); i++) {
                if (!lines[i].valid) {
                    to_write_vec_id = i;
                    lru_order.splice(lru_order.end(), lru_order, lines[i].lru_it);
                    printf("\tfind inv entry %u\n", to_write_vec_id);
                    break;
                }
            }
        }
        assert(to_write_vec_id < assoc);
        // assign mapping entry to this new line
        bool alloc_succ;
        std::tie(addr_map_it, alloc_succ) = addr_map.emplace(addr_base, to_write_vec_id);

        auto& entry = lines[addr_map_it->second];
        entry.map_it = addr_map_it;
        entry.valid = 1;
    }
    auto& entry = lines[addr_map_it->second];
    entry.dirty = 1;
    printf("\tfinal addr_map.size() = %lu\n", addr_map.size());
#ifndef NO_DATA
    if (mask == 0xFFFFFFFFFFFFFFFF) {
        for (int i = 0; i < AXI_WIDTH / 8; i++) {
            entry.spm_line[offset + i] = data[i];
        }
    } else {
        for (int i = 0; i < AXI_WIDTH / 8; i++) {
            if (!((mask >> i) & 1))
                continue;
            entry.spm_line[offset + i] = data[i];
        }
    }
#endif
}


void allBufferSet::clear_and_write_back_dirty() {
    for (auto& line: lines) {
        if (line.dirty) {
            wrapper->addDMAWriteReq(line.map_it->first, line.spm_line);
            line.dirty = 0;
        }
        addr_map.erase(line.map_it);
        line.valid = 0;
        // don't clear lru_order and the vector itself because they may be used for data coming in afterward
    }
}


void allBufferSet::fill_spm_line(uint64_t aligned_addr, const uint8_t* data) {
    assert((aligned_addr & (uint64_t)(spm_line_size - 1)) == 0);
    printf("allBufferSet::fill_spm_line(%#lx, %#lx)\n", aligned_addr, (uint64_t)data);

    auto addr_map_it = addr_map.find(aligned_addr);
    if (addr_map_it == addr_map.end()) {
        printf("\tnot found, allocate\n");
        uint32_t to_write_vec_id = assoc;
        if (addr_map.size() >= assoc) {
            printf("\tcurrent addr_map.size() = %lu\n", addr_map.size());
            to_write_vec_id = erase_victim();       // lru_order maintenance of erasing is done inside
            printf("\tafter erase addr_map.size() = %lu\n", addr_map.size());
            printf("\terase victim id %u\n", to_write_vec_id);
        } else {
            // find a currently invalid entry to write in
            for (uint32_t i = 0; i < lines.size(); i++) {
                if (!lines[i].valid) {
                    to_write_vec_id = i;
                    lru_order.splice(lru_order.end(), lru_order, lines[i].lru_it);
                    printf("\tfind inv entry %u\n", to_write_vec_id);
                    break;
                }
            }
        }
        assert(to_write_vec_id < assoc);
        // assign mapping entry to this new line
        bool alloc_succ;
        std::tie(addr_map_it, alloc_succ) = addr_map.emplace(aligned_addr, to_write_vec_id);
        auto& entry = lines[to_write_vec_id];
        entry.map_it = addr_map_it;
        entry.valid = 1;
        entry.dirty = 0;
        printf("\tfinal addr_map.size() = %lu\n", addr_map.size());
#ifndef NO_DATA
        entry.spm_line.assign(data, data + spm_line_size);
#endif
    } else {
        printf("Weird: request the DRAM when it hits the embedded buffer.\n");
    }
}


uint32_t allBufferSet::erase_victim() {
    auto to_erase_id = lru_order.front();
    auto& entry = lines[to_erase_id];
    assert(entry.valid);
    if (entry.dirty) {
        wrapper->addDMAWriteReq(entry.map_it->first, entry.spm_line);
    }
    addr_map.erase(entry.map_it);
    entry.valid = 0;
    lru_order.splice(lru_order.end(), lru_order, entry.lru_it);
    return to_erase_id;
}


prefetchThrottleSet::prefetchThrottleSet(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _assoc):
        abstractSet(wrap, _lat, _line_size, _assoc),
        lines(_assoc, prefetchThrottleLineWithTag(_line_size)) {

}


bool prefetchThrottleSet::read_spm_line(uint64_t aligned_addr, std::vector<uint8_t>& data_out) {
    assert((aligned_addr & (uint64_t)(spm_line_size - 1)) == 0);

    auto addr_map_it = addr_map.find(aligned_addr);
    if (addr_map_it == addr_map.end()) {
        return false;
    }
    auto& entry = lines[addr_map_it->second];
    entry.valid = 0;
    auto& line = entry.spm_line;
    data_out.assign(line.begin(), line.end());
    addr_map.erase(addr_map_it);
    return true;
}


void prefetchThrottleSet::clear_and_write_back_dirty() {
    for (auto& line: lines) {
        line.valid = 0;
        // don't clear lru_order and the vector itself because they may be used for data coming in afterward
    }
    addr_map.clear();
}


void prefetchThrottleSet::fill_spm_line(uint64_t aligned_addr, const uint8_t* data) {
    assert((aligned_addr & (uint64_t)(spm_line_size - 1)) == 0);
    assert(addr_map.find(aligned_addr) == addr_map.end());
    assert(addr_map.size() < assoc);

    uint32_t to_write_vec_id = assoc;
    for (uint32_t i = 0; i < lines.size(); i++) {
        if (!lines[i].valid) {
            to_write_vec_id = i;
            break;
        }
    }
    assert(to_write_vec_id < assoc);
    addr_map.emplace(aligned_addr, to_write_vec_id);
    auto& entry = lines[to_write_vec_id];
    entry.valid = 1;
#ifndef NO_DATA
    entry.spm_line.assign(data, data + spm_line_size);
#endif
}


embeddedBuffer::embeddedBuffer(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _line_num, uint32_t _assoc) :
        wrapper(wrap), spm_latency(_lat), spm_line_size(_line_size), spm_line_num(_line_num), assoc(_assoc),
        num_sets(_line_num / _assoc) {
    sets.reserve(_line_num / _assoc);
}


allBuffer::allBuffer(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _line_num, uint32_t _assoc) :
        embeddedBuffer(wrap, _lat, _line_size, _line_num, _assoc) {
    for (uint32_t set_id = 0; set_id < num_sets; set_id++) {
        sets.emplace_back(new allBufferSet(wrap, _lat, _line_size, _assoc));
    }
}


allBuffer::~allBuffer() {
    for (auto& set_ptr: sets) {
        delete set_ptr;
    }
}


bool allBuffer::read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out, uint8_t stream_id) {
    printf("allBuffer::read_spm_axi_line(%#lx, %#lx, %u)\n", axi_addr, (uint64_t)data_out, stream_id);
    uint32_t set_id = (axi_addr / spm_line_size) % num_sets;
    return sets[set_id]->read_spm_axi_line(axi_addr, data_out);
}


void allBuffer::write_spm_axi_line_with_mask(uint64_t axi_addr, const uint8_t* data, uint64_t mask, uint8_t stream) {
    printf("allBuffer::write_spm_axi_line_with_mask(%#lx, %#lx, %#lx, %u)\n", axi_addr, (uint64_t)data, mask, stream);
    uint32_t set_id = (axi_addr / spm_line_size) % num_sets;
    sets[set_id]->write_spm_axi_line_with_mask(axi_addr, data, mask);
}


void allBuffer::clear_and_write_back_dirty() {
    for (auto& set: sets) {
        set->clear_and_write_back_dirty();
    }
}


prefetchBuffer::prefetchBuffer(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _line_num, uint32_t _assoc) :
        embeddedBuffer(wrap, _lat, _line_size, _line_num, _assoc) {
    switch (wrap->buf_mode) {
        case BUF_MODE_PFT:
            for (uint32_t set_id = 0; set_id < num_sets; set_id++) {
                sets.emplace_back(new allBufferSet(wrap, _lat, _line_size, _assoc));
            }
            break;
        case BUF_MODE_PFT_CUTOFF:
            for (uint32_t set_id = 0; set_id < num_sets; set_id++) {
                sets.emplace_back(new prefetchThrottleSet(wrap, _lat, _line_size, _assoc));
            }
            break;
        case BUF_MODE_ALL:
        default:
            assert(false);
    }
}


prefetchBuffer::~prefetchBuffer() {
    for (auto& set_ptr: sets) {
        delete set_ptr;
    }
}


bool prefetchBuffer::read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out, uint8_t stream_id) {
    uint32_t set_id = (axi_addr / spm_line_size) % num_sets;
    uint64_t addr_base = axi_addr & ~(uint64_t)(spm_line_size - 1);
    if (addr_base != read_buffers[stream_id].first) {
        bool found = sets[set_id]->read_spm_line(addr_base, read_buffers[stream_id].second);
        if (!found) {
            return false;
        }
        read_buffers[stream_id].first = addr_base;
    }
    // always in read buffer here
#ifndef NO_DATA
    uint64_t offset = axi_addr & (uint64_t)(spm_line_size - 1);
    std::vector<uint8_t> &entry_vector = read_buffers[stream_id].second;
    for (int i = 0; i < AXI_WIDTH / 8; i++)
        data_out[i] = entry_vector[offset + i];
#endif
    return true;
}


uint32_t prefetchBuffer::num_valid(uint64_t try_addr) {
    uint32_t set_id = (try_addr / spm_line_size) % num_sets;
    return sets[set_id]->size();
};
