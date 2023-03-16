/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#ifndef BOARD_H
#define BOARD_H
#include "types.h"

Flag IsEmpty(int sqr);
void SetUp(char *fen, char *IDs, int var);
void StackMultis(Color c);
int PSTest();
int Dtest();
int MapAttacksByColor(Color color, int pieces, int level);
int MapAttacks(int level);
int MakeMove(Color stm, Move m, UndoInfo *u);
void UnMake(UndoInfo *u);
void pboard(int *b);
void pbytes(Flag *b);
void pmap(Color c);

extern VariantDesc *variant;
extern int bFiles, bRanks, zone, currentVariant, repDraws, stalemate;
#define chessFlag (currentVariant == V_CHESS || currentVariant == V_LION || currentVariant == V_WOLF)
#define chuFlag (currentVariant == V_CHU || currentVariant == V_LION)
#define makrukFlag (currentVariant == V_MAKRUK)
#define lionFlag (currentVariant == V_LION)
#define tenFlag (currentVariant == V_TENJIKU)
#define wolfFlag (currentVariant == V_WOLF)

extern int pVal;
extern int hashKeyH, hashKeyL, framePtr, rootEval, filling, promoDelta;
extern int level, cnt50;

extern Flag fireFlags[10]; // flags for Fire-Demon presence (last two are dummies, which stay 0, for compactify)

//                                           Main Data structures
//
// Attack table:
//   A table in board format, containing pairs of consecutive integers for each square (indexed as 2*sqr and 2*sqr+1)
//   The first integer contains info on black attacks to the square, the second similarly for white attacks
//   Each integer contains eight 3-bit fields, which count the number of attacks on it with moves in a particular direction
//   (If there are attacks by range-jumpers, the 3-bit count is increased by 2 over the actual value)

// Board:
//   The board has twice as many files as actually used, in 0x88 fashion
//   The used squares hold the piece numbers (for use as index in the piece list)
//   Unused squares are set to the invalid piece number EDGE
//   There are also 3 guard ranks of EDGE on each side

// Moves:
//   Moves are encoded as 11-bit from-square and to-square numbers packed in the low bits of an int
//   Special moves (like Lion double moves) are encoded by off-board to-squares above a certain value
//   Promotions are indicated by bit 22

// Hash table:
//   Entries of 16 bytes, holding a 32-bit signature, 16-bit lower- and upper-bound scores,
//   8-bit draft of each of those scores, an age counter that stores the search number of the last access.
//   The hash key is derived as the XOR of the products pieceKey[piece]*squareKey[square].

extern int board[BSIZE];
// Stockfish and many engines compute attacks on demand using magic bitboards
// whereas HaChu attempts in vain to maintain "this square attacks these rays"
// further compounded by "level" (search depth).
// There isn't a simple algorithm to generate and cache discovered attacks;
// therefore, attacksByLevel is recalculated for all pieces for both players
// after every move.  Please do not attempt to implement complex algorithms...
// it's not worth the complexity or effort, unless performance is critical
// and you are certain magic bitboards are inadequate.
// The question of "what squares does my piece attack?" could benefit from a map
// of squares to PieceDesc (or range) however "is a square attacked?" might not.
#define LEVELS 200
extern int attacksByLevel[LEVELS][COLORS][BSIZE];
#define attacks attacksByLevel[level]
#define ATTACK(pos, color) attacks[color][pos]
extern Flag fireBoard[BSIZE];    // flags to indicate squares controlled by Fire Demons
#endif
