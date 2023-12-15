#ifndef GEM5_NVDLA_EMBEDDEDBUFFER_HH
#define GEM5_NVDLA_EMBEDDEDBUFFER_HH

#include <queue>
#include <map>
#include <unordered_map>
#include <vector>

#include "wrapper_nvdla.hh"


class abstractSet {
protected:
    Wrapper_nvdla* const wrapper;
    const uint32_t spm_latency;
    const uint32_t spm_line_size;
    const uint32_t assoc;

    std::unordered_map<uint64_t, uint32_t> addr_map;
    // leave the vector to subclasses
public:
    abstractSet(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _assoc);
    virtual ~abstractSet() = 0;
    inline size_t size() { return addr_map.size(); }
    inline virtual bool read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out) { assert(false); }
    inline virtual bool read_spm_line(uint64_t aligned_addr, std::vector<uint8_t>& data_out) { assert(false); }
    inline virtual void write_spm_axi_line_with_mask(uint64_t axi_addr, const uint8_t* data, uint64_t mask) { assert(false); }
    virtual void fill_spm_line(uint64_t aligned_addr, const uint8_t* data) = 0;
};


class allBufferSet: public abstractSet {
protected:
    struct allBufferLineWithTag {
        std::vector<uint8_t> spm_line;
        std::list<uint32_t>::iterator lru_it;
        std::unordered_map<uint64_t, uint32_t>::iterator map_it;
        uint8_t dirty;
        uint8_t valid;

        allBufferLineWithTag(uint32_t spm_line_size, std::list<uint32_t>::iterator _lru_it,
                             std::unordered_map<uint64_t, uint32_t>::iterator _map_it):
                spm_line(spm_line_size, 0), lru_it(_lru_it), map_it(_map_it), dirty(0), valid(0) {}
    };
    std::vector<allBufferLineWithTag> lines;
    std::list<uint32_t> lru_order;

public:
    allBufferSet(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _assoc);
    ~allBufferSet() override = default;
    bool read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out) override;
    void write_spm_axi_line_with_mask(uint64_t axi_addr, const uint8_t* data, uint64_t mask) override;
    void fill_spm_line(uint64_t aligned_addr, const uint8_t* data) override;
    uint32_t erase_victim();
};


class prefetchThrottleSet: public abstractSet {
protected:
    struct prefetchThrottleLineWithTag {
        std::vector<uint8_t> spm_line;
        uint8_t valid;

        explicit prefetchThrottleLineWithTag(uint32_t spm_line_size) : spm_line(spm_line_size, 0), valid(0) {}
    };
    std::vector<prefetchThrottleLineWithTag> lines;

public:
    prefetchThrottleSet(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _assoc);
    ~prefetchThrottleSet() override = default;
    bool read_spm_line(uint64_t aligned_addr, std::vector<uint8_t>& data_out) override;
    void fill_spm_line(uint64_t aligned_addr, const uint8_t* data) override;
};


class Buffer {
protected:
    Wrapper_nvdla* const wrapper;

public:
    const uint32_t spm_latency;
    const uint32_t spm_line_size;   // all the sizes are in bytes
    const uint32_t spm_line_num;
    const uint32_t assoc;
    const uint32_t num_sets;

    explicit Buffer(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _line_num, uint32_t _assoc) :
        wrapper(wrap), spm_latency(_lat), spm_line_size(_line_size), spm_line_num(_line_num), assoc(_assoc),
        num_sets(_line_num / _assoc) {}
    virtual ~Buffer() = 0;
    virtual bool read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out, uint8_t stream_id) = 0;
    inline virtual void write_spm_axi_line_with_mask(uint64_t axi_addr, const uint8_t* data, uint64_t mask, uint8_t stream) {
        assert(false);
    }
    virtual void fill_spm_line(uint64_t aligned_addr, const uint8_t* data) = 0;
    inline virtual void write_back_dirty() { assert(false); }
};


template<typename SetType>
class embeddedBuffer: public Buffer {
protected:
    static_assert(std::is_base_of<abstractSet, SetType>::value, "SetType must be a descendant of abstractSet\n");
    std::vector<SetType> sets;

public:
    embeddedBuffer(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _line_num, uint32_t _assoc) :
        Buffer(wrap, _lat, _line_size, _line_num, _assoc),
        sets(_line_num / _assoc, SetType(wrap, _lat, _line_size, _assoc)) {}
    ~embeddedBuffer() override = default;


    void fill_spm_line(uint64_t aligned_addr, const uint8_t* data) override {
        uint32_t set_id = (aligned_addr / spm_line_size) % num_sets;
        sets[set_id].fill_spm_line(aligned_addr, data);
    }
};


class allBuffer: public embeddedBuffer<allBufferSet> {
public:
    allBuffer(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _line_num, uint32_t _assoc) :
            embeddedBuffer(wrap, _lat, _line_size, _line_num, _assoc) {}
    ~allBuffer() override = default;
    bool read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out, uint8_t stream_id) override;
    void write_spm_axi_line_with_mask(uint64_t axi_addr, const uint8_t* data, uint64_t mask, uint8_t stream) override;
};


template<typename SetType>
class prefetchBuffer: public embeddedBuffer<SetType> {
private:
    //! buffers for each stream (used only for reading)
    std::pair<uint64_t, std::vector<uint8_t> > read_buffers[10];

public:
    prefetchBuffer(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _line_num, uint32_t _assoc) :
            embeddedBuffer<SetType>(wrap, _lat, _line_size, _line_num, _assoc) {}
    ~prefetchBuffer() override = default;


    bool read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out, uint8_t stream_id) override {
        uint32_t set_id = (axi_addr / embeddedBuffer<SetType>::spm_line_size) % embeddedBuffer<SetType>::num_sets;
        uint64_t addr_base = axi_addr & ~(uint64_t)(embeddedBuffer<SetType>::spm_line_size - 1);
        if (addr_base != read_buffers[stream_id].first) {
            bool found = embeddedBuffer<SetType>::sets[set_id].read_spm_line(addr_base, read_buffers[stream_id].second);
            if (!found) {
                return false;
            }
            read_buffers[stream_id].first = addr_base;
        }
        // always in read buffer here
#ifndef NO_DATA
        uint64_t offset = axi_addr & (uint64_t)(embeddedBuffer<SetType>::spm_line_size - 1);
        std::vector<uint8_t> &entry_vector = read_buffers[stream_id].second;
        for (int i = 0; i < AXI_WIDTH / 8; i++)
            data_out[i] = entry_vector[offset + i];
#endif
        return true;
    }


    uint32_t num_valid(uint64_t try_addr) {
        uint32_t set_id = (try_addr / embeddedBuffer<SetType>::spm_line_size) % embeddedBuffer<SetType>::num_sets;
        return embeddedBuffer<SetType>::sets[set_id].size();
    };
};

#endif //GEM5_NVDLA_EMBEDDEDBUFFER_HH
