/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/

typedef int Color;
typedef unsigned char Flag;
typedef unsigned int Move;

typedef struct {
  int lock[5];
  int move[5];
  short int score[5];
  char depth[5];
  char flag[5];
  char age[4];
} HashBucket;

typedef struct {
  int from, to, piece, victim, new, booty, epSquare, epVictim[9], ep2Square, revMoveCount;
  int savKeyL, savKeyH, gain, loss, filling, saveDelta;
  char fireMask;
} UndoInfo;

typedef struct {
  int x, y;
} Vector;

#define RAYS 8
typedef struct {
  int pos;
  int pieceKey;
  int promo;
  int value;
  int pst;
  signed char range[RAYS];
  char promoFlag;
  char qval;
  char mobility;
  char mobWeight;
  Flag promoGain;
  char bulk;
  char ranking;
} PieceInfo; // piece-list entry

typedef struct {
  int lock;
  Move move;
  short int upper;
  short int lower;
  char depthU;
  char depthL;
  char flags;
  char age;
} HashEntry; // hash-table entry
