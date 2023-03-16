/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#ifndef EVAL_H
#define EVAL_H
#include "types.h"

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
#endif
