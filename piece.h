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

// Piece-Square Tables/Flags
#define PST_NEUTRAL  0
#define PST_STEPPER  1
#define PST_JUMPER   2
#define PST_SLIDER   3
#define PST_TRAP     4
#define PST_CENTER   5
#define PST_PPROM    6
#define PST_ZONDIST  7
#define PST_ADVANCE  8
#define PST_RETRACT  9
#define PST_FLYER   10
#define PST_LANCE   11
#define PSTSIZE     12 // number of PST types

PieceDesc *ListLookUp(char *name, PieceDesc *list);
PieceDesc *LookUp(char *name, int var);
void DeletePiece(Color c, int n);
int Worse(int a, int b);
int Lance(MoveType *r);
int EasyProm(MoveType *r);
Flag IsUpwardCompatible(MoveType *r, MoveType *s);
int ForwardOnly(MoveType *range);
int Range(MoveType *range);
void StackMultis(Color c);
void Compactify(Color c);
int AddPiece(Color c, PieceDesc *list);
void SetUp(char *fen, char *IDs, int var);
int myRandom();
void Init(int var);
int PSTest();
int Dtest();
int MapAttacksByColor(Color color, int pieces, int level);
int MapAttacks(int level);
int MakeMove(Color stm, Move m, UndoInfo *u);
void UnMake(UndoInfo *u);
void pplist();
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

extern Vector direction[2*RAYS];

extern int epList[104], ep2List[104], toList[104], reverse[104];  // decoding tables for double and triple moves
extern int kingStep[RAYS+2], knightStep[RAYS+2]; // raw tables for step vectors (indexed as -1 .. 8)
extern int neighbors[RAYS+1];                    // similar to kingStep, but starts with null-step
extern Flag fireFlags[10]; // flags for Fire-Demon presence (last two are dummies, which stay 0, for compactify)
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

// Promotion zones:
//   the promoBoard contains one byte with flags for each square, to indicate for each side whether the square
//   is in the promotion zone (twice), on the last two ranks, or on the last rank
//   the promoFlag field in the piece list can select which bits of this are tested, to see if it
//   (1) can promote (2) can defer when the to-square is on last rank, last two ranks, or anywhere.
//   Pawns normally can't defer anywhere, but if the user defers with them, their promoFlag is set to promote on last rank only

extern int pieces[COLORS], royal[COLORS], kylin[COLORS];
extern PieceInfo p[NPIECES]; // piece list

extern int squareKey[BSIZE];

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
extern Flag promoBoard[BSIZE];   // flags to indicate promotion zones
extern Flag fireBoard[BSIZE];    // flags to indicate squares controlled by Fire Demons
extern signed char psq[PSTSIZE][BSIZE]; // cache of piece-value-per-square
#define PSQ(type, sq, color) psq[type][color == BLACK ? sq : BSIZE-sq-1]

// Maximum of (ranks, files) of ray between squares
#define dist(s1, s2) MAX(abs((s1-s2)/BW), abs((s1-s2)%BW))
#endif
