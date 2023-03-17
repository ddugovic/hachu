/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#ifndef PIECE_H
#define PIECE_H
#include "types.h"

// promotion codes
#define CAN_PROMOTE 0x11
#define DONT_DEFER  0x22
#define CANT_DEFER  0x44
#define LAST_RANK   0x88
#define P_WHITE     0x0F
#define P_BLACK     0xF0

#define FVAL 5000 /* piece value of Fire Demon. Used in code for recognizing moves with it and do burns */
#define LVAL 1000 /* piece value of Lion. Used in chu for recognizing it to implement Lion-trade rules  */
#define DEMON(n) (p[n].value == FVAL)
#define LION(n)  (p[n].value == LVAL)

PieceDesc *ListLookUp(char *name, PieceDesc *list);
PieceDesc *LookUp(char *name, int var);
void DeletePiece(Color c, int n);
int Worse(int a, int b);
int Lance(MoveType *r);
int EasyProm(MoveType *r);
Flag IsUpwardCompatible(MoveType *r, MoveType *s);
int ForwardOnly(MoveType *range);
int Range(MoveType *range);
void Compactify(Color c);
int AddPiece(Color c, PieceDesc *list);
int myRandom();
void Init(int var);
void pplist();

extern Vector direction[2*RAYS];

extern int epList[104], ep2List[104], toList[104], reverse[104];  // decoding tables for double and triple moves
extern int kingStep[RAYS+2], knightStep[RAYS+2]; // raw tables for step vectors (indexed as -1 .. 8)
extern int neighbors[RAYS+1];                    // similar to kingStep, but starts with null-step
#define kStep (kingStep+1)
#define nStep (knightStep+1)

extern int attackMask[RAYS];
extern int rayMask[RAYS];
extern int ray[RAYS+1];

//                                           Main Data structures
//
// Piece List:
//   Interleaved lists for black and white, white taking the odd slots
//   Pieces in general have two entries: one for the basic, and one for the promoted version
//   The unused one gets its position set to the invalid square number ABSENT
//   The list is sorted by piece value, most valuable pieces first
//   When a piece is captured in the search, both its versions are marked ABSENT
//   In the root the list is packed to eliminate all captured pieces
//   The list contains a table for how far the piece moves in each direction
//   Range encoding: -3 = full Lion, -2 = on-ray Lion, -1 = plain jump, 0 = none, 1 = step, >1 = (limited) range

// Promotion zones:
//   the promoBoard contains one byte with flags for each square, to indicate for each side whether the square
//   is in the promotion zone (twice), on the last two ranks, or on the last rank
//   the promoFlag field in the piece list can select which bits of this are tested, to see if it
//   (1) can promote (2) can defer when the to-square is on last rank, last two ranks, or anywhere.
//   Pawns normally can't defer anywhere, but if the user defers with them, their promoFlag is set to promote on last rank only

extern int pieces[COLORS], royal[COLORS], kylin[COLORS];
extern PieceInfo p[NPIECES]; // piece list

extern int squareKey[BSIZE];

extern Flag promoBoard[BSIZE];   // flags to indicate promotion zones

// Maximum of (ranks, files) of ray between squares
#define dist(s1, s2) MAX(abs((s1-s2)/BW), abs((s1-s2)%BW))
#endif
