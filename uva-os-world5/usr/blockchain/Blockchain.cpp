#include "Blockchain.h"
#include "assert.h"
#include "string.h"

// Gen the Genesis Block
Blockchain::Blockchain(int difficulty) {    
    _vChain[0].block_init(0 /*idx*/, "Genesis Block"/*data in*/, ""/*prev hash*/);
    _nrBlock = 1;
    assert(difficulty <= MAX_DIFFICULTY);
    memset(_diffStr, '0', difficulty); // all "0"s with length of "difficulty"
    _diffStr[difficulty] = '\0';
}

// Geneate the next block
void Blockchain::AddBlock(uint32_t nIndexIn, const char *sDataIn) {
    assert(_nrBlock < MAXBLOCK);
    Block *b = &_vChain[_nrBlock];
    b->block_init(nIndexIn, sDataIn, _vChain[_nrBlock - 1].GetHash());
    // b->MineBlock(_diffStr); 
    b->MineBlockSMP(_diffStr); 
    _nrBlock++;
}
