/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#ifndef EVAL_H
#define EVAL_H
#include "types.h"

#define KYLIN 100 /* extra end-game value of Kylin for promotability */

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

extern signed char psq[PSTSIZE][BSIZE]; // cache of piece-value-per-square
#define PSQ(type, sq, color) psq[type][color == BLACK ? sq : BSIZE-sq-1]

typedef unsigned int HashKey;
extern HashKey hashKeyH, hashKeyL;
extern int rootEval, filling, promoDelta;
extern int mobilityScore;

typedef struct {
  int lock[5];
  Move move[5];
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

int Evaluate(Color c, int tsume, int difEval);
int Surround(Color c, int king, int start, int max);
int Fortress(int forward, int king, int lion);
int Ftest(int side);
int Guard(int sqr);

#endif
