#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>
#include "sha256.h"

class Block {
public:
  Block() { }
	void block_init(uint32_t nIndexIn, const char* sDataIn, const char* sPrevHashIn);
	const char* GetHash();
	void MineBlock(const char* diffStr);
	void MineBlockSMP(const char* diffStr);
	static void _CalculateHash_r(Block*, char*, char*, int); 
	static int miner(void *param); 
	static int miner_dummy(void *param); 

private:
	uint32_t _nIndex;
	uint32_t _nNonce;	// int as part of hash input
	const char *_sData;	
	char _sHash[2 * SHA256::DIGEST_SIZE + 1]; // saved block hash 
	const char *_sPrevHash; // hash of the prev block 
	uint32_t _tTime; // timestamp, as part of hash input
	void _CalculateHash(char *buf);
};
#endif
