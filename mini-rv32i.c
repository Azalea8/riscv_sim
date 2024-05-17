#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdint.h>

#define RAM_SIZE (64*1024*1024)
#define RAM_TEXT_START   0
#define RAM_TEXT_END     RAM_STACK_START
#define RAM_STACK_START (RAM_SIZE/2)
#define RAM_STACK_END    RAM_SIZE

enum RV32I_REG {
    Z,   
    RA, 
    SP, 
    GP,  
    TP,   
    T0,
    T1, 
    T2, 
    S0, 
    S1,     
    A0, 
    A1, 
    A2, 
    A3, 
    A4, 
    A5, 
    A6, 
    A7,
    S2, 
    S3, 
    S4, 
    S5, 
    S6, 
    S7, 
    S8, 
    S9, 
    S10, 
    S11,
    T3, 
    T4, 
    T5, 
    T6,
};

enum RV32I_CSR {
    PC,
};

struct CPUState {
    uint32_t regs[32], csrs[1];

    uint8_t *mem;
    uint32_t mem_offset, mem_size;
};

static inline int32_t rv32i_step(struct CPUState *state) {
    #define CSR(x) (state->csrs[x])
    #define REG(x) (state->regs[x])
    #define MEM(x) (&state->mem[x])

    uint32_t rval = 0;
    uint32_t pc = CSR(PC);
    
    uint32_t ir = 0;
    rval = 0;
    uint32_t ofs_pc = pc - state->mem_offset;

    ir = *(uint32_t *)MEM(ofs_pc);
    uint32_t rdid = (ir >> 7) & 0x1f;

    switch (ir & 0x7f) {
        case 0x37:{ // LUI (0b0110111)
            rval = (ir & 0xfffff000); 
            break;
        }
        case 0x17: {// AUIPC (0b0010111)
            rval = pc + (ir & 0xfffff000); 
            break;
        }
        case 0x6F: { // JAL (0b1101111)
            int32_t reladdy = ((ir & 0x80000000) >> 11) | ((ir & 0x7fe00000) >> 20) | ((ir & 0x00100000) >> 9) | ((ir & 0x000ff000));
            if (reladdy & 0x00100000)
                reladdy |= 0xffe00000;
            rval = pc + 4;
            pc = pc + reladdy - 4;
            break;
        }
        case 0x67: { // JALR (0b1100111)
            uint32_t imm = ir >> 20;
            int32_t imm_se = imm | ((imm & 0x800) ? 0xfffff000 : 0);
            rval = pc + 4;
            pc = ((REG((ir >> 15) & 0x1f) + imm_se) & ~1) - 4;
            break;
        }
        case 0x63: { // Branch (0b1100011)
            uint32_t immm4 = ((ir & 0xf00) >> 7) | ((ir & 0x7e000000) >> 20) | ((ir & 0x80) << 4) | ((ir >> 31) << 12);
            if (immm4 & 0x1000)
                immm4 |= 0xffffe000;
            int32_t rs1 = REG((ir >> 15) & 0x1f);
            int32_t rs2 = REG((ir >> 20) & 0x1f);
            immm4 = pc + immm4 - 4;
            rdid = 0;
            switch ((ir >> 12) & 0x7)
            {
                // BEQ, BNE, BLT, BGE, BLTU, BGEU
                case 0: if (rs1 == rs2) pc = immm4; break;
                case 1: if (rs1 != rs2) pc = immm4; break;
                case 4: if (rs1 < rs2) pc = immm4; break;
                case 5: if (rs1 >= rs2) pc = immm4; break; // BGE
                case 6: if ((uint32_t)rs1 < (uint32_t)rs2) pc = immm4; break; // BLTU
                case 7: if ((uint32_t)rs1 >= (uint32_t)rs2) pc = immm4; break; // BGEU
                break;
            }
        }
        case 0x03: { // Load (0b0000011)
            uint32_t rs1 = REG((ir >> 15) & 0x1f);
            uint32_t imm = ir >> 20;
            int32_t imm_se = imm | ((imm & 0x800) ? 0xfffff000 : 0);
            uint32_t rsval = rs1 + imm_se;

            rsval -= state->mem_offset;
            
            switch ((ir >> 12) & 0x7)
            {
                // LB, LH, LW, LBU, LHU
                case 0: rval = *(int8_t *)MEM(rsval); break;
                case 1: rval = *(int16_t *)MEM(rsval); break;
                case 2: rval = *(uint32_t *)MEM(rsval); break;
                case 4: rval = *(uint8_t *)MEM(rsval); break;
                case 5: rval = *(uint16_t *)MEM(rsval); break;
            }
            break;
        }
        case 0x23: { // Store (0b0100011)
            uint32_t rs1 = REG((ir >> 15) & 0x1f);
            uint32_t rs2 = REG((ir >> 20) & 0x1f);
            uint32_t addy = ((ir >> 7) & 0x1f) | ((ir & 0xfe000000) >> 20);
            if (addy & 0x800)
                addy |= 0xfffff000;
            addy += rs1 - state->mem_offset;
            rdid = 0;
            switch ((ir >> 12) & 0x7) { // SB, SH, SW
                case 0: *(uint8_t *)MEM(addy) = rs2; break;
                case 1: *(uint16_t *)MEM(addy) = rs2; break;
                case 2: *(uint32_t *)MEM(addy) = rs2; break;  
            }
            break;
        }
        case 0x13:   // Op-immediate 0b0010011
        case 0x33: { // Op           0b0110011
            uint32_t imm = ir >> 20;
            imm = imm | ((imm & 0x800) ? 0xfffff000 : 0);
            uint32_t rs1 = REG((ir >> 15) & 0x1f);
            uint32_t is_reg = !!(ir & 0x20);
            uint32_t rs2 = is_reg ? REG(imm & 0x1f) : imm;

            switch ((ir >> 12) & 7) {
                case 0: rval = (is_reg && (ir & 0x40000000)) ? (rs1 - rs2) : (rs1 + rs2); break;
                case 1: rval = rs1 << (rs2 & 0x1F); break;
                case 2: rval = (int32_t)rs1 < (int32_t)rs2; break;
                case 3: rval = rs1 < rs2; break;
                case 4: rval = rs1 ^ rs2; break;
                case 5: rval = (ir & 0x40000000) ? (((int32_t)rs1) >> (rs2 & 0x1F)) : (rs1 >> (rs2 & 0x1F)); break;
                case 6: rval = rs1 | rs2; break;
                case 7: rval = rs1 & rs2; break;
            }
            break;
        }
    }

    if (rdid) {
        state->regs[rdid] = rval;
    }

    pc += 4;

    CSR(PC) = pc;
    return 0;
}

void DumpState(struct CPUState * core) {
	uint32_t pc = core->csrs[PC];
	uint32_t pc_offset = pc - RAM_TEXT_START;
	uint32_t ir = 0;
	printf(" PC:%08x", pc);
	if (pc_offset >= 0 && pc_offset < RAM_TEXT_END - 3) {
		ir = *((uint32_t*)(&((uint8_t*)core->mem)[pc_offset]));
		printf(" [%08x]", ir);
	} else {
		printf(" [xxxxxxxx]");
    }
    printf("\n");
    uint32_t * regs = core->regs;
	printf("  Z:%08x  ra:%08x  sp:%08x  gp:%08x\n",  regs[Z], regs[RA], regs[SP], regs[GP]);
    printf(" tp:%08x  t0:%08x  t1:%08x  t2:%08x\n", regs[TP], regs[T0], regs[T1], regs[T2]);
    printf(" s0:%08x  s1:%08x  a0:%08x  a1:%08x\n", regs[S0], regs[S1], regs[A0], regs[A1]);
    printf(" a2:%08x  a3:%08x  a4:%08x  a5:%08x\n", regs[A2], regs[A3], regs[A4], regs[A5]);
	printf(" a6:%08x  a7:%08x  s2:%08x  s3:%08x\n", regs[A6], regs[A7], regs[S2], regs[S3]);
    printf(" s4:%08x  s5:%08x  s6:%08x  s7:%08x\n", regs[S4], regs[S5], regs[S6], regs[S7]);
    printf(" s8:%08x  s9:%08x s10:%08x s11:%08x\n", regs[S8], regs[S9], regs[S10], regs[S11]);
    printf(" t3:%08x  t4:%08x  t5:%08x  t6:%08x\n", regs[T3], regs[T4], regs[T5], regs[T6]);
    printf("stack (sp:%08x):\n", regs[SP]);
    for (int sp = regs[SP]; sp < core->mem_size; sp += sizeof(int32_t)) {
        printf("%08x: %08x\n", sp, core->mem[sp]);
    }
    printf("%#08x\n", RAM_STACK_END);
    printf("\n");
}

int xtoi(char * s) {
    int res = 0;
    for (int i = 2; s[i]; i ++) {
        res *= 16;
        if (s[i] >= 'a' && s[i] <= 'f') {
            res =+ s[i] - 'a';
        } else {
            res += s[i] - '0';
        }
    }
    return res;
}

int main(int argc, char ** argv) {
    // get the testcase
    if (argc < 2) {
        printf("Usage: ./mini-rv32i <path_testcase> <arg1> <arg2> ... <argn>\n");
        printf("- The testcase file should be a rv32i binary with 0 offset to the first line of instruction.\n");
        printf("- Note that we only support dec/hex int-type mainargs for simplicity.\n");
        return 0;
    }
    char * image_filename = argv[1];
    printf("[mini-rv32i] load image file: %s\n", image_filename);

    // allocate ram image
    struct CPUState state;
    printf("[mini-rv32i] alloc ram size = %#x\n", RAM_SIZE);
    memset(&state, 0, sizeof(state));
    state.mem = malloc(RAM_SIZE);
    if (!state.mem) {
		fprintf(stderr, "Error: failed to allocate ram image.\n");
		return 1;
	}
    memset(state.mem, 0, state.mem_size);
    state.mem_offset = RAM_TEXT_START;
    state.mem_size = RAM_SIZE;
	state.csrs[PC] = state.mem_offset;

    // load insts from testcase
    FILE * f = fopen(image_filename, "rb");
    if (!f || ferror(f)) {
        fprintf(stderr, "Error: image file \"%s\" not found\n", image_filename);
        return 1;
	}
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (flen > RAM_TEXT_END - RAM_TEXT_START) {
        fprintf(stderr, "Error: image file size too big (%#lx bytes)\n", flen);
        fclose(f);
        return 1;
    }
    if (fread(state.mem + RAM_TEXT_START, flen, 1, f) != 1) {
        fprintf(stderr, "Error: Failed to load image file\n");
        return 1;
    }
    fclose(f);


    // get mainargs
    #define MAX_MAINARGS 4
    if (argc > 2 + MAX_MAINARGS) {
        printf("[mini-rv32ima] WARN: mainargs should <= %d, the excess ones will be discarded\n", MAX_MAINARGS);
    }
    int margc = (argc > 2 + MAX_MAINARGS ? MAX_MAINARGS : argc - 2);
    int margs[MAX_MAINARGS] = {0};
    printf("[mini-rv32i] mainargs: ");
    for (int i = 0; i < margc; i ++) {
        if (argv[2 + i][0] == '0' && argv[2 + i][1] == 'x') {
            margs[i] = xtoi(argv[2 + i]);
        } else {
            margs[i] = atoi(argv[2 + i]);
        }
        printf("-%d ", margs[i]);
    }
    printf("\n");
    uint32_t sp = RAM_STACK_END - margc * sizeof(int32_t);
    memcpy(state.mem + sp, margs, RAM_STACK_END - sp);
	state.regs[SP] = sp;
	state.regs[A0] = margc;
	state.regs[A1] = sp;

    // do emulation
    printf("initially:\n");
    DumpState(&state);
    int ret;
    do {
        ret = rv32i_step(&state);
        if (ret != 0) printf("minirv32i ret=%d !=0\n", ret);
        DumpState(&state);
    } while (ret == 0 && state.csrs[PC] != 0);
    printf("finally:\n");
    DumpState(&state);
    // return
    return 0;
}