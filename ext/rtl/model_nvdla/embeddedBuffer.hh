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
    inline virtual void clear_and_write_back_dirty() { assert(false); }
    virtual void fill_spm_line(uint64_t aligned_addr, const uint8_t* data) = 0;
};


class allBufferSet: virtual public abstractSet {
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
    void clear_and_write_back_dirty() override;
    void fill_spm_line(uint64_t aligned_addr, const uint8_t* data) override;
    uint32_t erase_victim();
};


class prefetchThrottleSet: virtual public abstractSet {
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
    void clear_and_write_back_dirty() override;
    void fill_spm_line(uint64_t aligned_addr, const uint8_t* data) override;
};


class embeddedBuffer {
protected:
    Wrapper_nvdla* const wrapper;
    const uint32_t spm_line_num;
    std::vector<abstractSet*> sets;

public:
    const uint32_t spm_latency;
    const uint32_t spm_line_size;   // all the sizes are in bytes
    const uint32_t assoc;
    const uint32_t num_sets;

    embeddedBuffer(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _line_num, uint32_t _assoc);
    virtual bool read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out, uint8_t stream_id) = 0;
    virtual ~embeddedBuffer() = default;

    inline virtual void write_spm_axi_line_with_mask(uint64_t axi_addr, const uint8_t* data, uint64_t mask, uint8_t stream) {
        assert(false);
    }

    inline virtual void clear_and_write_back_dirty() {
        assert(false);
    }

    inline void fill_spm_line(uint64_t aligned_addr, const uint8_t* data) {
        uint32_t set_id = (aligned_addr / spm_line_size) % num_sets;
        sets[set_id]->fill_spm_line(aligned_addr, data);
    }
};


class allBuffer: virtual public embeddedBuffer {
public:
    allBuffer(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _line_num, uint32_t _assoc);
    ~allBuffer() override;
    bool read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out, uint8_t stream_id) override;
    void write_spm_axi_line_with_mask(uint64_t axi_addr, const uint8_t* data, uint64_t mask, uint8_t stream) override;
    void clear_and_write_back_dirty() override;
};


class prefetchBuffer: virtual public embeddedBuffer {
private:
    //! buffers for each stream (used only for reading)
    std::pair<uint64_t, std::vector<uint8_t> > read_buffers[10];

public:
    prefetchBuffer(Wrapper_nvdla* wrap, uint32_t _lat, uint32_t _line_size, uint32_t _line_num, uint32_t _assoc);
    ~prefetchBuffer() override;
    bool read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out, uint8_t stream_id) override;
    uint32_t num_valid(uint64_t try_addr);
};

#endif //GEM5_NVDLA_EMBEDDEDBUFFER_HH
