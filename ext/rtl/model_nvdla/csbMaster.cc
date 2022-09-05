#include "csbMaster.hh"

CSBMaster::CSBMaster(VNV_nvdla *_dla, Wrapper_nvdla *_wrapper) {
    dla = _dla;

    wrapper = _wrapper;

    dla->csb2nvdla_valid = 0;
    _test_passed = 1;
}

void CSBMaster::read(uint32_t addr, uint32_t mask, uint32_t data) {
    csb_op op;

    op.is_ext = 0;
    op.write = 0;
    op.addr = addr;
    op.mask = mask;
    op.data = data;
    op.tries = 10;
    op.reading = 0;
    op.wait_until = 0;

    opq.push(op);
}

void CSBMaster::wait_until(uint32_t addr, uint32_t mask, uint32_t data) {
    csb_op op;

    op.is_ext = 0;
    op.write = 0;
    op.addr = addr;
    op.mask = mask;
    op.data = data;
    op.tries = 10;
    op.reading = 0;
    op.wait_until = 1;

    opq.push(op);
}

void CSBMaster::write(uint32_t addr, uint32_t data) {
    csb_op op;

    op.is_ext = 0;
    op.write = 1;
    op.addr = addr;
    op.data = data;
    op.wait_until = 0;

    opq.push(op);
}

void CSBMaster::ext_event(int ext) {
    csb_op op;

    op.is_ext = ext;
    opq.push(op);
}

int CSBMaster::eval(int noop) {
    if (dla->nvdla2csb_wr_complete)
        printf("(%lu) write complete from CSB\n",
                wrapper->tickcount);

    dla->csb2nvdla_valid = 0;
    if (opq.empty())
        return 0;

    csb_op &op = opq.front();

    if (op.is_ext && !noop) {
        int ext = op.is_ext;
        opq.pop();

        return ext;
    }

    if (!op.write && op.reading && dla->nvdla2csb_valid) {
        if(op.wait_until == 0) {
            printf("(%lu) read response from nvdla: %08x\n",
                wrapper->tickcount, dla->nvdla2csb_data);
        }

        if ((dla->nvdla2csb_data & op.mask) != (op.data & op.mask)) {
            op.reading = 0;
            if(op.wait_until == 0) {
                if(op.write == 0 && op.addr == 0xffff0003 && op.data == 0x0 && dla->nvdla2csb_data != 0) {
                    printf("new interrupts come too early, so ignore this reg txn\n");
                    opq.pop();
                } else {
                    op.tries--;
                    printf("(%lu) invalid response -- trying again\n",
                           wrapper->tickcount);
                    if (!op.tries) {
                        printf("(%lu) ERROR: timed out reading response\n",
                               wrapper->tickcount);
                        _test_passed = 0;
                        opq.pop();
                    }
                }
            } else if(((dla->nvdla2csb_data & op.mask) & op.data) == op.data) {
                printf("(%lu) Intr %0x08x has the expected bit 0x%08x\n", wrapper->tickcount, dla->nvdla2csb_data, op.data);
                opq.pop();
            }
        } else {
            if(op.wait_until) printf("(%lu) Intr reg got the expected response 0x%08x\n", wrapper->tickcount, op.data);
            opq.pop();
        }
    }

    if (!op.write && op.reading)
        return 0;

    if (noop)
        return 0;

    if (!dla->csb2nvdla_ready) {
        printf("(%lu) CSB stalled...\n", wrapper->tickcount);
        return 0;
    }

    if (op.write) {
        dla->csb2nvdla_valid = 1;
        dla->csb2nvdla_addr = op.addr;
        dla->csb2nvdla_wdat = op.data;
        dla->csb2nvdla_write = 1;
        dla->csb2nvdla_nposted = 0;
        printf("(%lu) write to nvdla: addr %08x, data %08x\n",
                wrapper->tickcount, op.addr, op.data);
        opq.pop();
    } else {
        dla->csb2nvdla_valid = 1;
        dla->csb2nvdla_addr = op.addr;
        dla->csb2nvdla_write = 0;
        if(op.wait_until == 0) {
            printf("(%lu) read from nvdla: addr %08x\n",
                   wrapper->tickcount, op.addr);
        }
        op.reading = 1;
    }

    return 0;
}

bool CSBMaster::done() {
    return opq.empty();
}

int CSBMaster::test_passed() {
    return _test_passed;
}
