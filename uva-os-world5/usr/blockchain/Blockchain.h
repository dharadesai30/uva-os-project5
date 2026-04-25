#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include "Block.h"

#define MAX_DIFFICULTY 9
#define MAXBLOCK 10

class Blockchain {
public:
    Blockchain(int difficulty);
    void AddBlock(uint32_t nIndexIn, const char *sDataIn);

private:
    char _diffStr[MAX_DIFFICULTY + 3];
    Block _vChain[MAXBLOCK]; // blocks gen so far
    int _nrBlock;
};
#endif