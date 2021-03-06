#include "stdafx.h"
#include <cassert>
#include <iostream>
#include "magic.h"
#if defined(__GNUC__) && defined(__x86_64__) && !defined(__MINGW32__)
#include <x86intrin.h>
#endif

/**
* counts[index][moverBitPattern] contains the number of disks flipped in a row.
* @param index the index if the empty square in the pattern (so index=0 means the empty square is at the low-order bit)
* @param moverBitPattern 8 bits, each set if the corresponding square on the board is occupied by the mover
*/
int counts[8][256];

/**
* outsides[index][enemyBitPattern] is the first bit, on each side, that is 'outside' the closest enemy bits to the mover.
* for instance .**.**.., with index==3 (the middle empty '.'), the 'outside' bits are 
*              1.....1.
* If there is a mover disk in these spots, it will cause a flip.
**/
static uint8_t outsides[8][256];

/**
* insides[index][outsideBitPattern] is the disks that will be flipped if the mover had an outside disk in the given spots.
* For instance with index = 3 and outside disks at 
*   O.....O. , the inside (or flipped bit pattern) will be
*   .**.**..
*/
static uint8_t insides[8][256];

/**
* rowFlips[row][insideBitPattern] is the bitboard containing the disks that will be flipped
*/
u64 rowFlips[8][256];

/**
* colFlips[col][insideBitPattern] is the bitboard containing the disks that will be flipped
*/
static u64 colFlips[8][256];

const int nDiagonals = 11;
/**
* d9Flips[row-col+5][insideBitPattern] is the bitboard containing the disks that will be flipped.
* (if row-col < 5 then we can't flip along the diagonal, so we don't store the information)
*/
static u64 d9Flips[nDiagonals + 1][256];

/**
* d7Flips[row+col-2][insideBitPattern] is the bitboard containing the disks that will be flipped.
* (if row+col < 2 then we can't flip along the diagonal, so we don't store the information)
*/
u64 d7Flips[nDiagonals + 1][256];

/**
* Neighbors[square] is the bitboard containing disks adjacent to the square
*/
u64 neighbors[64];

static void initNeighbors() {
    for (int sq = 0; sq<64; sq++) {
        u64 m = mask(sq);
        if (col(sq)>0) {
            m|=m>>1;
        }
        if (col(sq)<7) {
            m|=m<<1;
        }
        m|=(m>>8)|(m<<8);
        neighbors[sq]=m&~mask(sq);
    }
}

static void initOutside(int index, int enemyBitPattern) {
    int outside = 0;

    for (int i=index-1; i>=0; i--) {
        if (bitClear(i, enemyBitPattern)) {
            outside |= 1<<i;
            break;
        }
    }
    for (int i=index+1; i<8; i++) {
        if (bitClear(i, enemyBitPattern)) {
            outside |= 1<<i;
            break;
        }
    }
    outsides[index][enemyBitPattern]=outside;
}

static void initInside(int index, int moverBitPattern) {
    int count = 0;
    int inside = 0;
    int insideLeft = 0;
    for (int i=index-1; i>=0; i--) {
        if (bitSet(i, moverBitPattern)) {
            count+=index-i-1;
            inside|= insideLeft;
            break;
        }
        else {
            insideLeft |= 1<<i;
        }
    }

    int insideRight = 0;
    for (int i=index+1; i<8; i++) {
        if (bitSet(i, moverBitPattern)) {
            inside|= insideRight;
            count+=i-index-1;
            break;
        }
        else {
            insideRight|= 1<<i;
        }
    }
    counts[index][moverBitPattern]=count;
    insides[index][moverBitPattern] = inside;
}

static void initRowFlips(int row, u64 insideBitPattern) {
    rowFlips[row][insideBitPattern] = insideBitPattern << (row*8);
}

static void initColFlips(int col, u64 insideBitPattern) {
    // turn pattern sideways using magic
    u64 pattern = (insideBitPattern * 0x02040810204081) & MaskA;

    colFlips[col][insideBitPattern] = pattern << col;
}

static u64 signedLeftShift(u64 pattern, int shift) {
    if (shift > 0) {
        return pattern<<shift;
    }
    else {
        return pattern>>-shift;
    }
}

/**
* @param index row-col+5
*/
static void initD9Flips(int index, u64 insideBitPattern) {
    // turn pattern diagonally using magic
    u64 pattern = (insideBitPattern * MaskA) & MaskA1H8;
    int diff = index-5; // diff =row-col

    d9Flips[index][insideBitPattern] = signedLeftShift(pattern, diff*8);
}

static void initD7Flips(int index, u64 insideBitPattern) {
    // turn pattern diagonally using magic
    u64 pattern = (insideBitPattern * MaskA) & MaskA8H1;

    int diff = index-5; // diff = row+col-7

    d7Flips[index][insideBitPattern] = signedLeftShift(pattern, diff*8);
}

void initFlips() {
    initNeighbors();
    for (int bitPattern=0; bitPattern<256; bitPattern++) {
        for (int index=0; index<8; index++) {
            initOutside(index, bitPattern);
            initInside(index, bitPattern);
            initRowFlips(index, bitPattern);
            initColFlips(index, bitPattern);
        }
        for (int index=0; index<nDiagonals; index++) {
            initD9Flips(index, bitPattern);
            initD7Flips(index, bitPattern);
        }
        d9Flips[nDiagonals][bitPattern] = 0;
        d7Flips[nDiagonals][bitPattern] = 0;
    }
    for (int index=0; index<8; index++) {
        assert(counts[index][0] == 0);
    }
}

inline int flipIndex(int moveLoc, u64 mover, u64 enemy, u64 mask, u64 mult) {
    const u64 enemy256 = (enemy&mask)*mult>>56;
    const int out = outsides[moveLoc][enemy256];
    const u64 mover256 = (mover&mask)*mult>>56;
    const int flipIndex = insides[moveLoc][mover256&out];
    return flipIndex;
}

inline int rowFlipIndex(int row, int col, u64 mover, u64 enemy) {
    const int shift = row<<3;
    const u64 enemy256 = (enemy>>shift)&0xFF;
    const int out = outsides[col][enemy256];
    const int flipIndex = insides[col][(mover>>shift)&out];
    return flipIndex;
}


struct magicFlip {
uint64_t d9mask;
uint64_t d9mult;
uint64_t colmask;
uint64_t colmult;
uint64_t d7mask;
uint64_t d7mult;
uint32_t d9b;
uint32_t d7b;
uint64_t _;  // filler so that the structure has 64 bytes
};

static struct magicFlip flipArray[64] = {
{ 0x8040201008040201ULL,  0x101010101010101ULL,  0x101010101010101ULL,  0x102040810204080ULL,  0ULL,  0ULL,  5,  11 },
{ 0x80402010080402ULL,  0x101010101010101ULL,  0x202020202020202ULL,  0x81020408102040ULL,  0ULL,  0ULL,  4,  11 },
{ 0x804020100804ULL,  0x101010101010101ULL,  0x404040404040404ULL,  0x40810204081020ULL,  0x10204ULL,  0x101010101010101ULL,  3,  0 },
{ 0x8040201008ULL,  0x101010101010101ULL,  0x808080808080808ULL,  0x20408102040810ULL,  0x1020408ULL,  0x101010101010101ULL,  2,  1 },
{ 0x80402010ULL,  0x101010101010101ULL,  0x1010101010101010ULL,  0x10204081020408ULL,  0x102040810ULL,  0x101010101010101ULL,  1,  2 },
{ 0x804020ULL,  0x101010101010101ULL,  0x2020202020202020ULL,  0x8102040810204ULL,  0x10204081020ULL,  0x101010101010101ULL,  0,  3 },
{ 0ULL,  0ULL,  0x4040404040404040ULL,  0x4081020408102ULL,  0x1020408102040ULL,  0x101010101010101ULL,  11,  4 },
{ 0ULL,  0ULL,  0x8080808080808080ULL,  0x2040810204081ULL,  0x102040810204080ULL,  0x101010101010101ULL,  11,  5 },
{ 0x4020100804020100ULL,  0x101010101010101ULL,  0x101010101010101ULL,  0x102040810204080ULL,  0ULL,  0ULL,  6,  11 },
{ 0x8040201008040201ULL,  0x101010101010101ULL,  0x202020202020202ULL,  0x81020408102040ULL,  0ULL,  0ULL,  5,  11 },
{ 0x80402010080402ULL,  0x101010101010101ULL,  0x404040404040404ULL,  0x40810204081020ULL,  0x1020408ULL,  0x101010101010101ULL,  4,  1 },
{ 0x804020100804ULL,  0x101010101010101ULL,  0x808080808080808ULL,  0x20408102040810ULL,  0x102040810ULL,  0x101010101010101ULL,  3,  2 },
{ 0x8040201008ULL,  0x101010101010101ULL,  0x1010101010101010ULL,  0x10204081020408ULL,  0x10204081020ULL,  0x101010101010101ULL,  2,  3 },
{ 0x80402010ULL,  0x101010101010101ULL,  0x2020202020202020ULL,  0x8102040810204ULL,  0x1020408102040ULL,  0x101010101010101ULL,  1,  4 },
{ 0ULL,  0ULL,  0x4040404040404040ULL,  0x4081020408102ULL,  0x102040810204080ULL,  0x101010101010101ULL,  11,  5 },
{ 0ULL,  0ULL,  0x8080808080808080ULL,  0x2040810204081ULL,  0x204081020408000ULL,  0x101010101010101ULL,  11,  6 },
{ 0x2010080402010000ULL,  0x101010101010101ULL,  0x101010101010101ULL,  0x102040810204080ULL,  0x10204ULL,  0x101010101010101ULL,  7,  0 },
{ 0x4020100804020100ULL,  0x101010101010101ULL,  0x202020202020202ULL,  0x81020408102040ULL,  0x1020408ULL,  0x101010101010101ULL,  6,  1 },
{ 0x8040201008040201ULL,  0x101010101010101ULL,  0x404040404040404ULL,  0x40810204081020ULL,  0x102040810ULL,  0x101010101010101ULL,  5,  2 },
{ 0x80402010080402ULL,  0x101010101010101ULL,  0x808080808080808ULL,  0x20408102040810ULL,  0x10204081020ULL,  0x101010101010101ULL,  4,  3 },
{ 0x804020100804ULL,  0x101010101010101ULL,  0x1010101010101010ULL,  0x10204081020408ULL,  0x1020408102040ULL,  0x101010101010101ULL,  3,  4 },
{ 0x8040201008ULL,  0x101010101010101ULL,  0x2020202020202020ULL,  0x8102040810204ULL,  0x102040810204080ULL,  0x101010101010101ULL,  2,  5 },
{ 0x80402010ULL,  0x101010101010101ULL,  0x4040404040404040ULL,  0x4081020408102ULL,  0x204081020408000ULL,  0x101010101010101ULL,  1,  6 },
{ 0x804020ULL,  0x101010101010101ULL,  0x8080808080808080ULL,  0x2040810204081ULL,  0x408102040800000ULL,  0x101010101010101ULL,  0,  7 },
{ 0x1008040201000000ULL,  0x101010101010101ULL,  0x101010101010101ULL,  0x102040810204080ULL,  0x1020408ULL,  0x101010101010101ULL,  8,  1 },
{ 0x2010080402010000ULL,  0x101010101010101ULL,  0x202020202020202ULL,  0x81020408102040ULL,  0x102040810ULL,  0x101010101010101ULL,  7,  2 },
{ 0x4020100804020100ULL,  0x101010101010101ULL,  0x404040404040404ULL,  0x40810204081020ULL,  0x10204081020ULL,  0x101010101010101ULL,  6,  3 },
{ 0x8040201008040201ULL,  0x101010101010101ULL,  0x808080808080808ULL,  0x20408102040810ULL,  0x1020408102040ULL,  0x101010101010101ULL,  5,  4 },
{ 0x80402010080402ULL,  0x101010101010101ULL,  0x1010101010101010ULL,  0x10204081020408ULL,  0x102040810204080ULL,  0x101010101010101ULL,  4,  5 },
{ 0x804020100804ULL,  0x101010101010101ULL,  0x2020202020202020ULL,  0x8102040810204ULL,  0x204081020408000ULL,  0x101010101010101ULL,  3,  6 },
{ 0x8040201008ULL,  0x101010101010101ULL,  0x4040404040404040ULL,  0x4081020408102ULL,  0x408102040800000ULL,  0x101010101010101ULL,  2,  7 },
{ 0x80402010ULL,  0x101010101010101ULL,  0x8080808080808080ULL,  0x2040810204081ULL,  0x810204080000000ULL,  0x101010101010101ULL,  1,  8 },
{ 0x804020100000000ULL,  0x101010101010101ULL,  0x101010101010101ULL,  0x102040810204080ULL,  0x102040810ULL,  0x101010101010101ULL,  9,  2 },
{ 0x1008040201000000ULL,  0x101010101010101ULL,  0x202020202020202ULL,  0x81020408102040ULL,  0x10204081020ULL,  0x101010101010101ULL,  8,  3 },
{ 0x2010080402010000ULL,  0x101010101010101ULL,  0x404040404040404ULL,  0x40810204081020ULL,  0x1020408102040ULL,  0x101010101010101ULL,  7,  4 },
{ 0x4020100804020100ULL,  0x101010101010101ULL,  0x808080808080808ULL,  0x20408102040810ULL,  0x102040810204080ULL,  0x101010101010101ULL,  6,  5 },
{ 0x8040201008040201ULL,  0x101010101010101ULL,  0x1010101010101010ULL,  0x10204081020408ULL,  0x204081020408000ULL,  0x101010101010101ULL,  5,  6 },
{ 0x80402010080402ULL,  0x101010101010101ULL,  0x2020202020202020ULL,  0x8102040810204ULL,  0x408102040800000ULL,  0x101010101010101ULL,  4,  7 },
{ 0x804020100804ULL,  0x101010101010101ULL,  0x4040404040404040ULL,  0x4081020408102ULL,  0x810204080000000ULL,  0x101010101010101ULL,  3,  8 },
{ 0x8040201008ULL,  0x101010101010101ULL,  0x8080808080808080ULL,  0x2040810204081ULL,  0x1020408000000000ULL,  0x101010101010101ULL,  2,  9 },
{ 0x402010000000000ULL,  0x101010101010101ULL,  0x101010101010101ULL,  0x102040810204080ULL,  0x10204081020ULL,  0x101010101010101ULL,  10,  3 },
{ 0x804020100000000ULL,  0x101010101010101ULL,  0x202020202020202ULL,  0x81020408102040ULL,  0x1020408102040ULL,  0x101010101010101ULL,  9,  4 },
{ 0x1008040201000000ULL,  0x101010101010101ULL,  0x404040404040404ULL,  0x40810204081020ULL,  0x102040810204080ULL,  0x101010101010101ULL,  8,  5 },
{ 0x2010080402010000ULL,  0x101010101010101ULL,  0x808080808080808ULL,  0x20408102040810ULL,  0x204081020408000ULL,  0x101010101010101ULL,  7,  6 },
{ 0x4020100804020100ULL,  0x101010101010101ULL,  0x1010101010101010ULL,  0x10204081020408ULL,  0x408102040800000ULL,  0x101010101010101ULL,  6,  7 },
{ 0x8040201008040201ULL,  0x101010101010101ULL,  0x2020202020202020ULL,  0x8102040810204ULL,  0x810204080000000ULL,  0x101010101010101ULL,  5,  8 },
{ 0x80402010080402ULL,  0x101010101010101ULL,  0x4040404040404040ULL,  0x4081020408102ULL,  0x1020408000000000ULL,  0x101010101010101ULL,  4,  9 },
{ 0x804020100804ULL,  0x101010101010101ULL,  0x8080808080808080ULL,  0x2040810204081ULL,  0x2040800000000000ULL,  0x101010101010101ULL,  3,  10 },
{ 0ULL,  0ULL,  0x101010101010101ULL,  0x102040810204080ULL,  0x1020408102040ULL,  0x101010101010101ULL,  11,  4 },
{ 0ULL,  0ULL,  0x202020202020202ULL,  0x81020408102040ULL,  0x102040810204080ULL,  0x101010101010101ULL,  11,  5 },
{ 0x804020100000000ULL,  0x101010101010101ULL,  0x404040404040404ULL,  0x40810204081020ULL,  0x204081020408000ULL,  0x101010101010101ULL,  9,  6 },
{ 0x1008040201000000ULL,  0x101010101010101ULL,  0x808080808080808ULL,  0x20408102040810ULL,  0x408102040800000ULL,  0x101010101010101ULL,  8,  7 },
{ 0x2010080402010000ULL,  0x101010101010101ULL,  0x1010101010101010ULL,  0x10204081020408ULL,  0x810204080000000ULL,  0x101010101010101ULL,  7,  8 },
{ 0x4020100804020100ULL,  0x101010101010101ULL,  0x2020202020202020ULL,  0x8102040810204ULL,  0x1020408000000000ULL,  0x101010101010101ULL,  6,  9 },
{ 0x8040201008040201ULL,  0x101010101010101ULL,  0x4040404040404040ULL,  0x4081020408102ULL,  0ULL,  0ULL,  5,  11 },
{ 0x80402010080402ULL,  0x101010101010101ULL,  0x8080808080808080ULL,  0x2040810204081ULL,  0ULL,  0ULL,  4,  11 },
{ 0ULL,  0ULL,  0x101010101010101ULL,  0x102040810204080ULL,  0x102040810204080ULL,  0x101010101010101ULL,  11,  5 },
{ 0ULL,  0ULL,  0x202020202020202ULL,  0x81020408102040ULL,  0x204081020408000ULL,  0x101010101010101ULL,  11,  6 },
{ 0x402010000000000ULL,  0x101010101010101ULL,  0x404040404040404ULL,  0x40810204081020ULL,  0x408102040800000ULL,  0x101010101010101ULL,  10,  7 },
{ 0x804020100000000ULL,  0x101010101010101ULL,  0x808080808080808ULL,  0x20408102040810ULL,  0x810204080000000ULL,  0x101010101010101ULL,  9,  8 },
{ 0x1008040201000000ULL,  0x101010101010101ULL,  0x1010101010101010ULL,  0x10204081020408ULL,  0x1020408000000000ULL,  0x101010101010101ULL,  8,  9 },
{ 0x2010080402010000ULL,  0x101010101010101ULL,  0x2020202020202020ULL,  0x8102040810204ULL,  0x2040800000000000ULL,  0x101010101010101ULL,  7,  10 },
{ 0x4020100804020100ULL,  0x101010101010101ULL,  0x4040404040404040ULL,  0x4081020408102ULL,  0ULL,  0ULL,  6,  11 },
{ 0x8040201008040201ULL,  0x101010101010101ULL,  0x8080808080808080ULL,  0x2040810204081ULL,  0ULL,  0ULL,  5,  11 }}; 

struct dflip {
	uint64_t d9mask;
	uint64_t d7mask;
};
#if defined(__GNUC__) && defined(__x86_64__) && !defined(__MINGW32__)
__attribute__((target("default")))
#endif
u64 flips(int sq, u64 mover, u64 enemy) {
    if (neighbors[sq]&enemy) {
        const struct magicFlip &m = flipArray[sq];
        const int row = sq >> 3;
        const int col = sq & 7;

        u64 flip=0;

        const int rowIndex = rowFlipIndex(row, col, mover, enemy);
        flip |= rowFlips[row][rowIndex];
        const int d9Index = flipIndex(col, mover, enemy, m.d9mask, m.d9mult);
        flip |= d9Flips[m.d9b][d9Index];
        const int colIndex = flipIndex(row, mover, enemy, m.colmask, m.colmult);
        flip |= colFlips[col][colIndex];
        const int d7Index = flipIndex(col, mover, enemy, m.d7mask, m.d7mult);
        flip |= d7Flips[m.d7b][d7Index];

        return flip;
    } else {
        return 0;
    }
}

#if defined(__GNUC__) && defined(__x86_64__) && !defined(__MINGW32__)
static struct dflip dflip_array[64] = {
	{ 0x8040201008040201ULL, 0ULL },
	{ 0x80402010080402ULL, 0ULL },
	{ 0x804020100804ULL, 0x10204ULL },
	{ 0x8040201008ULL, 0x1020408ULL },
	{ 0x80402010ULL, 0x102040810ULL },
	{ 0x804020ULL, 0x10204081020ULL },
	{ 0ULL, 0x1020408102040ULL },
	{ 0ULL, 0x102040810204080ULL },
	{ 0x4020100804020100ULL, 0ULL },
	{ 0x8040201008040201ULL, 0ULL },
	{ 0x80402010080402ULL, 0x1020408ULL },
	{ 0x804020100804ULL, 0x102040810ULL },
	{ 0x8040201008ULL, 0x10204081020ULL },
	{ 0x80402010ULL, 0x1020408102040ULL },
	{ 0ULL, 0x102040810204080ULL },
	{ 0ULL, 0x204081020408000ULL },
	{ 0x2010080402010000ULL, 0x10204ULL },
	{ 0x4020100804020100ULL, 0x1020408ULL },
	{ 0x8040201008040201ULL, 0x102040810ULL },
	{ 0x80402010080402ULL, 0x10204081020ULL },
	{ 0x804020100804ULL, 0x1020408102040ULL },
	{ 0x8040201008ULL, 0x102040810204080ULL },
	{ 0x80402010ULL, 0x204081020408000ULL },
	{ 0x804020ULL, 0x408102040800000ULL },
	{ 0x1008040201000000ULL, 0x1020408ULL },
	{ 0x2010080402010000ULL, 0x102040810ULL },
	{ 0x4020100804020100ULL, 0x10204081020ULL },
	{ 0x8040201008040201ULL, 0x1020408102040ULL },
	{ 0x80402010080402ULL, 0x102040810204080ULL },
	{ 0x804020100804ULL, 0x204081020408000ULL },
	{ 0x8040201008ULL, 0x408102040800000ULL },
	{ 0x80402010ULL, 0x810204080000000ULL },
	{ 0x804020100000000ULL, 0x102040810ULL },
	{ 0x1008040201000000ULL, 0x10204081020ULL },
	{ 0x2010080402010000ULL, 0x1020408102040ULL },
	{ 0x4020100804020100ULL, 0x102040810204080ULL },
	{ 0x8040201008040201ULL, 0x204081020408000ULL },
	{ 0x80402010080402ULL, 0x408102040800000ULL },
	{ 0x804020100804ULL, 0x810204080000000ULL },
	{ 0x8040201008ULL, 0x1020408000000000ULL },
	{ 0x402010000000000ULL, 0x10204081020ULL },
	{ 0x804020100000000ULL, 0x1020408102040ULL },
	{ 0x1008040201000000ULL, 0x102040810204080ULL },
	{ 0x2010080402010000ULL, 0x204081020408000ULL },
	{ 0x4020100804020100ULL, 0x408102040800000ULL },
	{ 0x8040201008040201ULL, 0x810204080000000ULL },
	{ 0x80402010080402ULL, 0x1020408000000000ULL },
	{ 0x804020100804ULL, 0x2040800000000000ULL },
	{ 0ULL, 0x1020408102040ULL },
	{ 0ULL, 0x102040810204080ULL },
	{ 0x804020100000000ULL, 0x204081020408000ULL },
	{ 0x1008040201000000ULL, 0x408102040800000ULL },
	{ 0x2010080402010000ULL, 0x810204080000000ULL },
	{ 0x4020100804020100ULL, 0x1020408000000000ULL },
	{ 0x8040201008040201ULL, 0ULL },
	{ 0x80402010080402ULL, 0ULL },
	{ 0ULL, 0x102040810204080ULL },
	{ 0ULL, 0x204081020408000ULL },
	{ 0x402010000000000ULL, 0x408102040800000ULL },
	{ 0x804020100000000ULL, 0x810204080000000ULL },
	{ 0x1008040201000000ULL, 0x1020408000000000ULL },
	{ 0x2010080402010000ULL, 0x2040800000000000ULL },
	{ 0x4020100804020100ULL, 0ULL },
	{ 0x8040201008040201ULL, 0ULL }};

__attribute__((target("bmi2")))
u64 flips(int sq, u64 mover, u64 enemy) {
    if (neighbors[sq]&enemy) {
        const struct dflip &m = dflip_array[sq];
        const u64 row = sq >> 3;
        const u64 col = sq & 7;

        u64 flip=0;
        {
            const auto shift = row * 8;
            const auto enemy256 = (enemy>>shift)&0xFF;
            uint64_t flipIndex = insides[col][(mover>>shift)&outsides[col][enemy256]];
            flip |= flipIndex << shift;
        }
        {
            const auto enemy256_extr = _pext_u64(enemy, m.d9mask);
            const auto mover256_extr = _pext_u64(mover, m.d9mask);
            const auto pos = std::min(row, col);
            flip |= _pdep_u64(insides[pos][mover256_extr&outsides[pos][enemy256_extr]], m.d9mask);
        }
        {
            const auto colmask = 0x0101010101010101ULL << col;
            const auto enemy256_extr = _pext_u64(enemy, colmask);
            const auto mover256_extr = _pext_u64(mover, colmask);
            flip |= _pdep_u64(insides[row][mover256_extr&outsides[row][enemy256_extr]], colmask);
        }
        {
            const auto enemy256_extr = _pext_u64(enemy, m.d7mask);
            const auto mover256_extr = _pext_u64(mover, m.d7mask);
            const auto pos = std::min(row, 7 - col);
            flip |= _pdep_u64(insides[pos][mover256_extr&outsides[pos][enemy256_extr]], m.d7mask);
        }

        return flip;
    } else {
        return 0;
    }
}

/* not used, I need to figure out a better algorithm for diagonal masks */
__attribute__((target("bmi2")))
u64 flips_bmi2_noref(int sq, u64 mover, u64 enemy) {
    constexpr u64 main_diag = 0x8040201008040201ULL;
    constexpr u64 sec_diag  = 0x0102040810204080ULL;
    if (neighbors[sq]&enemy) {
        const u64 row = sq >> 3;
        const u64 col = sq & 7;

        u64 flip=0;
        {
            const auto shift = row * 8;
            const auto enemy256 = (enemy>>shift)&0xFF;
            uint64_t flipIndex = insides[col][(mover>>shift)&outsides[col][enemy256]];
            flip |= flipIndex << shift;
        }
        {
            auto mask = (main_diag >> 8 * std::max<int64_t>(col - row, 0LL)) << 8 * std::max<int64_t>(row - col, 0LL);
            const auto enemy256_extr = _pext_u64(enemy, mask);
            const auto mover256_extr = _pext_u64(mover, mask);
            const auto pos = std::min(row, col);
            auto ins = insides[pos][mover256_extr&outsides[pos][enemy256_extr]];
            flip |= _pdep_u64(ins, mask);
        }
        {
            const auto colmask = 0x0101010101010101ULL << col;
            const auto enemy256_extr = _pext_u64(enemy, colmask);
            const auto mover256_extr = _pext_u64(mover, colmask);
            flip |= _pdep_u64(insides[row][mover256_extr&outsides[row][enemy256_extr]], colmask);
        }
        {
            auto mask = (sec_diag >> 8 * std::max<int64_t>(7 - col - row, 0LL)) << 8 * std::max<int64_t>(row - 7 + col, 0LL);
            const auto enemy256_extr = _pext_u64(enemy, mask);
            const auto mover256_extr = _pext_u64(mover, mask);
            const auto pos = std::min(row, 7 - col);
            auto ins = insides[pos][mover256_extr&outsides[pos][enemy256_extr]];
            flip |= _pdep_u64(ins, mask);
        }

        return flip;
    } else {
        return 0;
    }
}
#endif


struct magicCount {
    uint64_t mask1;
    uint64_t mask2;
    uint64_t mask3;
    uint64_t mult2;
};

static struct magicCount magicCountArray[64] = {
{ 0x8040201008040201ULL,  0x101010101010101ULL,  0ULL,  0x102040810204080ULL },
{ 0x80402010080402ULL,  0x202020202020202ULL,  0ULL,  0x81020408102040ULL },
{ 0x804020100804ULL,  0x404040404040404ULL,  0x10204ULL,  0x40810204081020ULL },
{ 0x8040201008ULL,  0x808080808080808ULL,  0x1020408ULL,  0x20408102040810ULL },
{ 0x80402010ULL,  0x1010101010101010ULL,  0x102040810ULL,  0x10204081020408ULL },
{ 0x804020ULL,  0x2020202020202020ULL,  0x10204081020ULL,  0x8102040810204ULL },
{ 0ULL,  0x4040404040404040ULL,  0x1020408102040ULL,  0x4081020408102ULL },
{ 0ULL,  0x8080808080808080ULL,  0x102040810204080ULL,  0x2040810204081ULL },
{ 0x4020100804020100ULL,  0x101010101010101ULL,  0ULL,  0x102040810204080ULL },
{ 0x8040201008040201ULL,  0x202020202020202ULL,  0ULL,  0x81020408102040ULL },
{ 0x80402010080402ULL,  0x404040404040404ULL,  0x1020408ULL,  0x40810204081020ULL },
{ 0x804020100804ULL,  0x808080808080808ULL,  0x102040810ULL,  0x20408102040810ULL },
{ 0x8040201008ULL,  0x1010101010101010ULL,  0x10204081020ULL,  0x10204081020408ULL },
{ 0x80402010ULL,  0x2020202020202020ULL,  0x1020408102040ULL,  0x8102040810204ULL },
{ 0ULL,  0x4040404040404040ULL,  0x102040810204080ULL,  0x4081020408102ULL },
{ 0ULL,  0x8080808080808080ULL,  0x204081020408000ULL,  0x2040810204081ULL },
{ 0x2010080402010000ULL,  0x101010101010101ULL,  0x10204ULL,  0x102040810204080ULL },
{ 0x4020100804020100ULL,  0x202020202020202ULL,  0x1020408ULL,  0x81020408102040ULL },
{ 0x8040201008040201ULL,  0x404040404040404ULL,  0x102040810ULL,  0x40810204081020ULL },
{ 0x80402010080402ULL,  0x808080808080808ULL,  0x10204081020ULL,  0x20408102040810ULL },
{ 0x804020100804ULL,  0x1010101010101010ULL,  0x1020408102040ULL,  0x10204081020408ULL },
{ 0x8040201008ULL,  0x2020202020202020ULL,  0x102040810204080ULL,  0x8102040810204ULL },
{ 0x80402010ULL,  0x4040404040404040ULL,  0x204081020408000ULL,  0x4081020408102ULL },
{ 0x804020ULL,  0x8080808080808080ULL,  0x408102040800000ULL,  0x2040810204081ULL },
{ 0x1008040201000000ULL,  0x101010101010101ULL,  0x1020408ULL,  0x102040810204080ULL },
{ 0x2010080402010000ULL,  0x202020202020202ULL,  0x102040810ULL,  0x81020408102040ULL },
{ 0x4020100804020100ULL,  0x404040404040404ULL,  0x10204081020ULL,  0x40810204081020ULL },
{ 0x8040201008040201ULL,  0x808080808080808ULL,  0x1020408102040ULL,  0x20408102040810ULL },
{ 0x80402010080402ULL,  0x1010101010101010ULL,  0x102040810204080ULL,  0x10204081020408ULL },
{ 0x804020100804ULL,  0x2020202020202020ULL,  0x204081020408000ULL,  0x8102040810204ULL },
{ 0x8040201008ULL,  0x4040404040404040ULL,  0x408102040800000ULL,  0x4081020408102ULL },
{ 0x80402010ULL,  0x8080808080808080ULL,  0x810204080000000ULL,  0x2040810204081ULL },
{ 0x804020100000000ULL,  0x101010101010101ULL,  0x102040810ULL,  0x102040810204080ULL },
{ 0x1008040201000000ULL,  0x202020202020202ULL,  0x10204081020ULL,  0x81020408102040ULL },
{ 0x2010080402010000ULL,  0x404040404040404ULL,  0x1020408102040ULL,  0x40810204081020ULL },
{ 0x4020100804020100ULL,  0x808080808080808ULL,  0x102040810204080ULL,  0x20408102040810ULL },
{ 0x8040201008040201ULL,  0x1010101010101010ULL,  0x204081020408000ULL,  0x10204081020408ULL },
{ 0x80402010080402ULL,  0x2020202020202020ULL,  0x408102040800000ULL,  0x8102040810204ULL },
{ 0x804020100804ULL,  0x4040404040404040ULL,  0x810204080000000ULL,  0x4081020408102ULL },
{ 0x8040201008ULL,  0x8080808080808080ULL,  0x1020408000000000ULL,  0x2040810204081ULL },
{ 0x402010000000000ULL,  0x101010101010101ULL,  0x10204081020ULL,  0x102040810204080ULL },
{ 0x804020100000000ULL,  0x202020202020202ULL,  0x1020408102040ULL,  0x81020408102040ULL },
{ 0x1008040201000000ULL,  0x404040404040404ULL,  0x102040810204080ULL,  0x40810204081020ULL },
{ 0x2010080402010000ULL,  0x808080808080808ULL,  0x204081020408000ULL,  0x20408102040810ULL },
{ 0x4020100804020100ULL,  0x1010101010101010ULL,  0x408102040800000ULL,  0x10204081020408ULL },
{ 0x8040201008040201ULL,  0x2020202020202020ULL,  0x810204080000000ULL,  0x8102040810204ULL },
{ 0x80402010080402ULL,  0x4040404040404040ULL,  0x1020408000000000ULL,  0x4081020408102ULL },
{ 0x804020100804ULL,  0x8080808080808080ULL,  0x2040800000000000ULL,  0x2040810204081ULL },
{ 0ULL,  0x101010101010101ULL,  0x1020408102040ULL,  0x102040810204080ULL },
{ 0ULL,  0x202020202020202ULL,  0x102040810204080ULL,  0x81020408102040ULL },
{ 0x804020100000000ULL,  0x404040404040404ULL,  0x204081020408000ULL,  0x40810204081020ULL },
{ 0x1008040201000000ULL,  0x808080808080808ULL,  0x408102040800000ULL,  0x20408102040810ULL },
{ 0x2010080402010000ULL,  0x1010101010101010ULL,  0x810204080000000ULL,  0x10204081020408ULL },
{ 0x4020100804020100ULL,  0x2020202020202020ULL,  0x1020408000000000ULL,  0x8102040810204ULL },
{ 0x8040201008040201ULL,  0x4040404040404040ULL,  0ULL,  0x4081020408102ULL },
{ 0x80402010080402ULL,  0x8080808080808080ULL,  0ULL,  0x2040810204081ULL },
{ 0ULL,  0x101010101010101ULL,  0x102040810204080ULL,  0x102040810204080ULL },
{ 0ULL,  0x202020202020202ULL,  0x204081020408000ULL,  0x81020408102040ULL },
{ 0x402010000000000ULL,  0x404040404040404ULL,  0x408102040800000ULL,  0x40810204081020ULL },
{ 0x804020100000000ULL,  0x808080808080808ULL,  0x810204080000000ULL,  0x20408102040810ULL },
{ 0x1008040201000000ULL,  0x1010101010101010ULL,  0x1020408000000000ULL,  0x10204081020408ULL },
{ 0x2010080402010000ULL,  0x2020202020202020ULL,  0x2040800000000000ULL,  0x8102040810204ULL },
{ 0x4020100804020100ULL,  0x4040404040404040ULL,  0ULL,  0x4081020408102ULL },
{ 0x8040201008040201ULL,  0x8080808080808080ULL,  0ULL,  0x2040810204081ULL }
};

/**
* Calculate # of disks flipped, given that there is only 1 empty left
* Because there's only one empty, and we know the mover bitboard and the empty square, we don't need the enemy bitboard
* @param mover mover bitboard
* @return number of disks flipped with 1 empty
*/
int lastFlipCount(int sq, u64 mover) {
    if (neighbors[sq]&~mover) {
        const struct magicCount &m = magicCountArray[sq];
        const int row = sq >> 3;
        const int col = sq & 7;
        int v =
            counts[col][(mover >> (row * 8)) & 0xFF] +
            counts[col][(mover & m.mask1) * MaskA >> 56] +
            counts[row][(mover & m.mask2) * m.mult2 >> 56] +
            counts[col][(mover & m.mask3) * MaskA >> 56];
        return v;
    }
    else {
        return 0;
    }
}
