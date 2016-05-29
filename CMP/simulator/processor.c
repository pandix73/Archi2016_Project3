#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "define.h"  

// project 1
FILE *i_file, *d_file, *report, *snap;
int i_size, d_size;
char *i_buffer, *d_buffer;
size_t i_result, d_result;
unsigned int PC, i_memory[1026];
unsigned int sp, d_data[1026];
unsigned char d_memory[1024];
unsigned int reg[32];
int cycle = 0;

// CMP var: i as 0, d as 1
int MEM_size[2] = {64, 32};
int Page_size[2] = {8, 16};
int C_size[2] = {16, 16};
int C_block[2] = {4, 4};
int C_associate[2] = {4, 1};
void print_vm();
int check_TLB(int VPN, int ID);
int check_PTE(int VPN, int ID);


// CMP
typedef struct _TEntry{
	int VPN;
	int PPN;
	int LRU; // least recently use, -1 for invalid
}TEntry;

typedef struct _PEntry{ // VPN as index
	int PPN;
	int valid;
}PEntry;

typedef struct _CEntry{
	int tag;
	int valid;
	int MRU;
}CEntry;

typedef struct _MEntry{
	int LRU;
}MEntry;

TEntry T[1024][2];
PEntry P[1024][2];
CEntry C[1024][1024][2];
MEntry M[1024][2];
int T_size[2];
int P_size[2];
int C_row[2];
int C_col[2];
int M_size[2];

int T_hits[2];
int T_miss[2];
int P_hits[2];
int P_miss[2];
int C_hits[2];
int C_miss[2];

void initialize(){
	int i, k;
	for(i = 0; i < 1024; i++){
		memset(&T[i][0], 0, sizeof(TEntry));
		memset(&T[i][1], 0, sizeof(TEntry));
		memset(&P[i][0], 0, sizeof(PEntry));
		memset(&P[i][1], 0, sizeof(PEntry));
		memset(&M[i][0], 0, sizeof(MEntry));
		memset(&M[i][1], 0, sizeof(MEntry));
		for(k = 0; k < 1024; k++){
			memset(&C[i][k][0], 0, sizeof(CEntry));
			memset(&C[i][k][1], 0, sizeof(CEntry));
		}
	}
}


int check_TLB(int VPN, int ID){
	//if(!ID)printf("%d->", T_miss[0] + T_hits[0]);
	int i;
	for(i = 0; i < T_size[ID]; i++){
		if(T[i][ID].VPN == VPN){ // hit
			T_hits[ID]++;
			//if(!ID)printf("%d\n\n", T_miss[0] + T_hits[0]);
			T[i][ID].LRU = cycle;
			return T[i][ID].PPN;
		}
	}
	T_miss[ID]++;
	//if(!ID)printf("%d\n\n", T_miss[0] + T_hits[0]);
	return check_PTE(VPN, ID);
}

void update_TLB(int VPN, int ID){
	int i;
	int min = -1;
	for(i = 0; i < T_size[ID]; i++){ // update T
		if(T[i][ID].LRU == -1){
			min = i;
			break;
		} else if(min == -1 || T[min][ID].LRU > T[i][ID].LRU){
			min = i;
		}
	}
	T[min][ID].VPN = VPN;
	T[min][ID].PPN = P[VPN][ID].PPN;
	T[min][ID].LRU = cycle;
}

int check_PTE(int VPN, int ID){
	if(P[VPN][ID].valid == 1){ // hit
		P_hits[ID]++;
		update_TLB(VPN, ID);
		return P[VPN][ID].PPN;
	} else { // miss
		P_miss[ID]++;
		int i, min = -1;
		for(i = 0; i < M_size[ID]; i++){
			if(min == -1 || M[i][ID].LRU < M[min][ID].LRU)
				min = i;
		}
		M[min][ID].LRU = cycle; // min as new PPN
		// delete original tlb and pte
		for(i = 0; i < P_size[ID]; i++)
			if(P[i][ID].PPN == min){
				P[i][ID].valid = 0;
				break;
			}
		for(i = 0; i < T_size[ID]; i++)
			if(T[i][ID].PPN == min){
				T[i][ID].LRU = -1;
				break;
			}
		//update tlb and pte
		P[VPN][ID].PPN = min; 
		P[VPN][ID].valid = 1;
		update_TLB(VPN, ID);
		return P[VPN][ID].PPN;
	}
}

void check_Cache(int PA, int VPN, int ID){
	int index = PA / C_block[ID] % C_row[ID];
	int tag = PA / C_block[ID] / C_row[ID];
	int i;
	for(i = 0; i < C_col[ID]; i++){
		if(C[index][i][ID].valid == 1 && C[index][i][ID].tag == tag){ // hit
			C_hits[ID]++;
			return;
		}
	}
	//miss
	
	C_miss[ID]++;
	int swap_valid = -1; // by valid
	int swap_MRU = -1; // by MRU
	int trans = 1;
	for(i = 0; i < C_col[ID]; i++){
		if(C[index][i][ID].valid == 0 && swap_valid == -1)
			swap_valid = i;
		if(C[index][i][ID].MRU == 0 && swap_MRU == -1)
			swap_MRU = i;
	}
	int swap = (swap_valid == -1) ? swap_MRU : swap_valid;
	
	for(i = 0; i < C_col[ID]; i++)
		if(i != swap && C[index][i][ID].MRU == 0)
			trans = 0;
	if(trans == 1)
		for(i = 0; i < C_col[ID]; i++)
			if(i != swap)
				C[index][i][ID].MRU = 0;
	printf("MRU:");
	for(i = 0; i < C_col[ID]; i++)
		printf("%d ", C[index][i][ID].MRU);
	printf("\n");
	
	C[index][swap][ID].tag = tag;
	printf("%d %d %d %d\n", index, swap, ID, tag);
	C[index][swap][ID].valid = 1;
	C[index][swap][ID].MRU = (C_col[ID] == 1) ? 0 : 1;
	return;
}

void vm(int VA, int ID){
	//printf("%c, %d\n", (ID == 0) ? 'I' : 'D', VA);
	int VPN = VA / Page_size[ID];
	int PPN = check_TLB(VPN, ID);
	int PA = PPN * Page_size[ID] + VPN % Page_size[ID];
	//if(!ID)printf("%d -> ", C_miss[0] + C_hits[0]);
	check_Cache(PA, VPN, ID);
	//if(!ID)printf("%d\n\n", C_miss[0] + C_hits[0]);
}

void print_vm(){
	fprintf( report, "ICache :\n");
	fprintf( report, "# hits: %u\n", C_hits[0] );
	fprintf( report, "# misses: %u\n\n", C_miss[0] );
	fprintf( report, "DCache :\n");
	fprintf( report, "# hits: %u\n", C_hits[1] );
	fprintf( report, "# misses: %u\n\n", C_miss[1] );
	fprintf( report, "ITLB :\n");
	fprintf( report, "# hits: %u\n", T_hits[0] );
	fprintf( report, "# misses: %u\n\n", T_miss[0] );
	fprintf( report, "DTLB :\n");
	fprintf( report, "# hits: %u\n", T_hits[1] );
	fprintf( report, "# misses: %u\n\n", T_miss[1] );
	fprintf( report, "IPageTable :\n");
	fprintf( report, "# hits: %u\n", P_hits[0] );
	fprintf( report, "# misses: %u\n\n", P_miss[0] );
	fprintf( report, "DPageTable :\n");
	fprintf( report, "# hits: %u\n", P_hits[1] );
	fprintf( report, "# misses: %u\n\n", P_miss[1] );
}

//project 1	
void read_d_memory(){
	int i;
	for(i = 0; i < 2; i++){
		//change 12 34 56 78  to  78 56 34 12
		d_data[i] = d_data[i] << 24 | d_data[i] >> 8 << 24 >> 8 | d_data[i] >> 16 << 24 >> 16 | d_data[i] >> 24;
	}
	for(i = 2; i < 2+d_data[1]; i++){
		//change 12 34 56 78  to  78 56 34 12
		d_data[i] = d_data[i] << 24 | d_data[i] >> 8 << 24 >> 8 | d_data[i] >> 16 << 24 >> 16 | d_data[i] >> 24;
	}
	reg[29] = d_data[0];
	for(i = 0; i < d_data[1]; i++){
		d_memory[i*4] = d_data[i+2] >> 24;
		d_memory[i*4 + 1] = d_data[i+2] << 8 >> 24;
		d_memory[i*4 + 2] = d_data[i+2] << 16 >> 24;
		d_memory[i*4 + 3] = d_data[i+2] << 24 >> 24;
	}
}

void read_i_memory(){
	unsigned int i, opcode;
	int funct, rs, rt, rd, shamt, C_26;
	short C;
	for(i = 0; i < 2; i++){
		//change 12 34 56 78  to  78 56 34 12
		i_memory[i] = i_memory[i] << 24 | i_memory[i] >> 8 << 24 >> 8 | i_memory[i] >> 16 << 24 >> 16 | i_memory[i] >> 24;
	}
	for(i = 2; i < 2+i_memory[1]; i++){
		//change 12 34 56 78  to  78 56 34 12
		i_memory[i] = i_memory[i] << 24 | i_memory[i] >> 8 << 24 >> 8 | i_memory[i] >> 16 << 24 >> 16 | i_memory[i] >> 24;
	}
	PC = i_memory[0];

	fprintf(snap, "cycle %d\n", cycle++);
	for(i = 0; i < 32; i++){
		fprintf(snap, "$%02d: 0x%08X\n", i, reg[i]);
	}fprintf(snap, "PC: 0x%08X\n\n\n", PC);
	
	for(i = 2; i < 2+i_memory[1]; i = ((PC-i_memory[0]) >> 2) + 2){
		vm((i - 2) * 4, 0);
		opcode = i_memory[i] >> 26;
		PC = PC + 4;
		int w0error = 0;
		
		switch(opcode){
			case R: 
				funct = i_memory[i] << 26 >> 26;
				rs = i_memory[i] << 6 >> 27;
				rt = i_memory[i] << 11 >> 27;
				rd = i_memory[i] << 16 >> 27;
				shamt = i_memory[i] << 21 >> 27;
				
				switch(funct){
					case add:
						reg[rd] = reg[rs] + reg[rt];
						break;
					case addu:
						reg[rd] = reg[rs] + reg[rt];
						break;
					case sub:
						reg[rd] = reg[rs] + (~reg[rt] + 1);
						break;
					case and:
						reg[rd] = reg[rs] & reg[rt];
						break;
					case or:
						reg[rd] = reg[rs] | reg[rt];
						break;
					case xor:
						reg[rd] = reg[rs] ^ reg[rt];
						break;
					case nor:
						reg[rd] = ~(reg[rs] | reg[rt]);
						break;
					case nand:
						reg[rd] = ~(reg[rs] & reg[rt]);
						break;
					case slt:
						reg[rd] = ((int)reg[rs] < (int)reg[rt]);
						break;
					case sll:
						reg[rd] = reg[rt] << shamt;
						break;
					case srl:
						reg[rd] = reg[rt] >> shamt;
						break;
					case sra:
						reg[rd] = (int)reg[rt] >> shamt;
						break;
					case jr:
						PC = reg[rs];
						break;
				}
				break;
			case j:
				C_26 = i_memory[i] << 6 >> 6;
				PC = PC >> 28 << 28 | (unsigned int)C_26 << 2;
				break;
			case jal:
				C_26 = i_memory[i] << 6 >> 6;
				reg[31] = PC;
				PC = PC >> 28 << 28 | (unsigned int)C_26 << 2;
				break;
			case halt: 
				return;
				break;
			default:
				rs = i_memory[i] << 6 >> 27;
				rt = i_memory[i] << 11 >> 27;
				C = i_memory[i] << 16 >> 16;
				int addr = (int)reg[rs] + (int)C;

				switch(opcode){
					case addi:
						reg[rt] = addr;
						break;
					case addiu:
						reg[rt] = addr;
						break;
					case lw:
						vm(addr, 1);
						reg[rt] = d_memory[addr] << 24 | d_memory[addr+1] << 16 | d_memory[addr+2] << 8 | d_memory[addr+3];
						break;
					case lh:
						vm(addr, 1);
						reg[rt] = (char)d_memory[addr] << 8 | (unsigned char)d_memory[addr+1];
						break;
					case lhu:
						vm(addr, 1);
						reg[rt] = (unsigned char)d_memory[addr] << 8 | (unsigned char)d_memory[addr+1];
						break;
					case lb:
						vm(addr, 1);
						reg[rt] = (char)d_memory[reg[rs] + C];
						break;
					case lbu:
						vm(addr, 1);
						reg[rt] = (unsigned char)d_memory[reg[rs] + C];
						break;
					case sw:
						vm(addr, 1);
						d_memory[addr] = reg[rt] >> 24;
						d_memory[addr+1] = reg[rt] << 8 >> 24;
						d_memory[addr+2] = reg[rt] << 16 >> 24;
						d_memory[addr+3] = reg[rt] << 24 >> 24;
						break;
					case sh:
						vm(addr, 1);
						d_memory[addr] = reg[rt] << 16 >> 24;
						d_memory[addr+1] = reg[rt] << 24 >> 24;
						break;
					case sb:
						vm(addr, 1);
						d_memory[addr] = reg[rt] << 24 >> 24;
						break;
					case lui:
						reg[rt] = C << 16;
						break;
					case andi:
						reg[rt] = reg[rs] & (unsigned short)C;
						break;
					case ori:
						reg[rt] = reg[rs] | (unsigned short)C;
						break;
					case nori:
						reg[rt] = ~(reg[rs] | (unsigned short)C);
						break;
					case slti:
						reg[rt] = ((int)reg[rs] < C) ? 1 : 0;
						break;
					case beq:
						if(reg[rs] == reg[rt])
							PC += C << 2;
						break;
					case bne:
						if(reg[rs] != reg[rt])
							PC += C << 2;
						break;
					case bgtz:
						if((int)reg[rs] > 0)
							PC += C << 2;
						break;
				}
				break;
		}

		fprintf(snap, "cycle %d\n", cycle++);
		int reg_n;
		for(reg_n = 0; reg_n < 32; reg_n++){
			fprintf(snap, "$%02d: 0x%08X\n", reg_n, reg[reg_n]);
		}fprintf(snap, "PC: 0x%08X\n\n\n", PC);
		
		while(PC < i_memory[0]){
			PC += 4;
			fprintf(snap, "cycle %d\n", cycle++);
			for(reg_n = 0; reg_n < 32; reg_n++){
				fprintf(snap, "$%02d: 0x%08X\n", reg_n, reg[reg_n]);
			}fprintf(snap, "PC: 0x%08X\n\n\n", PC);
		}
	}
}

int main (int argc, char *args[]) {

	if (argc == 11) {
        MEM_size[0] = atoi(args[1]);
        MEM_size[1] = atoi(args[2]);
        Page_size[0] = atoi(args[3]);
        Page_size[1] = atoi(args[4]);
        C_size[0] = atoi(args[5]);
        C_block[0] = atoi(args[6]);
        C_associate[0] = atoi(args[7]);
        C_block[1] = atoi(args[8]);
        C_block[1] = atoi(args[9]);
        C_associate[1] = atoi(args[10]);
    } else {
		//default
	}
	
	i_file = fopen("iimage.bin", "rb");
	d_file = fopen("dimage.bin", "rb");
	report = fopen("report.rpt", "w");
	snap = fopen("snapshot.rpt", "w");

	if (i_file == NULL || d_file == NULL) {fputs ("File error",stderr); exit (1);}

	// obtain file size:
	fseek (i_file , 0 , SEEK_END);
	fseek (d_file , 0 , SEEK_END);
	i_size = ftell(i_file);
	if(i_size > 1026)i_size = 1026;
	d_size = ftell(d_file);
	if(d_size > 1026)d_size = 1026;
	rewind(i_file);
	rewind(d_file);

	// copy the file into the buffer:
	i_result = fread(i_memory, 4, i_size/4, i_file);
	d_result = fread(d_data  , 4, d_size/4, d_file);
	
	P_size[0] = (i_size-2) / Page_size[0];
	P_size[1] = (d_size-2) / Page_size[1];
	T_size[0] = P_size[0] / 4;
	T_size[1] = P_size[1] / 4;
	C_row[0] = C_size[0] / C_associate[0] / C_block[0];
	C_row[1] = C_size[1] / C_associate[1] / C_block[1];
	C_col[0] = C_associate[0];
	C_col[1] = C_associate[1];
	M_size[0] = MEM_size[0] / Page_size[0];
	M_size[1] = MEM_size[1] / Page_size[1];
	
	initialize();
	read_d_memory(); 
	read_i_memory();
	print_vm();
	
	// terminate
	fclose(i_file);
	fclose(d_file);
	fclose(report);
	fclose(snap);
	free(i_buffer);
	free(d_buffer);
	return 0;
}


