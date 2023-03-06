/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#ifndef TYPES_H
#define TYPES_H

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define BH 16
#define BW 16
#define BSIZE BW*BH
#define STEP(X,Y) (BW*(X)+(Y))
#define POS(X,Y) STEP((BH-bRanks)/2 + X, (BW-bFiles)/2 + Y)
#define LL POS(0, 0)
#define LR POS(0, bFiles-1)
#define UL POS(bRanks-1, 0)
#define UR POS(bRanks-1, bFiles-1)
#define FILECH(s) (((s)-LL)%BW+'a')
#define RANK(s) (((s)-LL)/BW+1)
#define MOVE(from, to) (from<<SQLEN | to)

#define SQLEN      9           /* bits in square (or absent/special) number */
#define INVALID    0           /* cannot occur as a valid move   */
#define SPECIAL  400           /* start of special moves         */
#define BURN    (SPECIAL+96)   /* start of burn encodings        */
#define CASTLE  (SPECIAL+100)  /* castling encodings (4)         */
#define ABSENT  (1<<(SQLEN-1)) /* removed from board (PieceInfo) */
#define EDGE    (1<<(SQLEN-1)) /* off the board (0x88-style)     */
#define NPIECES ABSENT+1       /* length of piece + absent lists */
#define SQUARE  ((1<<SQLEN)-1) /* mask for square in move        */
#define FROM(move) (move>>SQLEN & SQUARE)

#define DEFER   (1<<2*SQLEN)   /* deferral on zone entry */
#define PROMOTE (1<<2*SQLEN+1) /* promotion bit in move  */

#define BLACK      0
#define WHITE      1
#define COLORS     2
#define INVERT(C) (WHITE+BLACK-C)
#define EMPTY      0           /* piece type (empty square) */
#define TYPE    (WHITE|BLACK|EDGE)

#define SORTKEY(X) 0
#define RAYS       8
#define RAY(X,Y) (RAYS*(X)+(Y))

typedef unsigned char Color;
typedef unsigned char Flag;
typedef unsigned int Move;
typedef signed char MoveType;

typedef struct {
  int lock[5];
  int move[5];
  short int score[5];
  char depth[5];
  char flag[5];
  char age[4];
} HashBucket;

typedef struct {
  int lock;
  Move move;
  Flag upper; // bound type
  Flag lower; // bound type
  char depthU;
  char depthL;
  char flags;
  char age;
} HashEntry; // hash-table entry

typedef struct {
  char *name, *promoted;
  int value;
  MoveType range[RAYS];
  char bulk;
  char ranking;
  int whiteKey, blackKey;
} PieceDesc;

typedef struct {
  int pos;
  int pieceKey;
  int promo;
  int value;
  int pst;
  MoveType range[RAYS];
  Flag promoFlag;
  char qval;
  char mobility;
  char mobWeight;
  Flag promoGain;
  char bulk;
  char ranking;
} PieceInfo; // piece-list entry

typedef struct {
  int from, to, piece, victim, new, booty, epSquare, epVictim[RAYS+1], ep2Square, revMoveCount;
  int savKeyL, savKeyH, gain, loss, filling, saveDelta;
  Flag fireMask;
} UndoInfo;

typedef struct {
  int boardFiles, boardRanks, zoneDepth, varNr; // board sizes
  char *name;  // WinBoard name
  char *array; // initial position
  char *IDs;
} VariantDesc;

typedef struct {
  int x, y;
} Vector;
#endif
