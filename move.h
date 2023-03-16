/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#ifndef MOVE_H
#define MOVE_H

#include "types.h"

#define REP_MASK 0xFFFFFF

extern int sup0, sup1, sup2; // promo suppression squares
extern int repCnt;
extern char *reason;

MoveInfo MoveToInfo(Move move);     // unboxes (from, to, path)
char *MoveToText(Move move, int m); // converts the move from your internal format to text like e2e2, e1g1, a7a8q.
Move ParseMove(Color stm, int ls, int le, char *moveText, Move *moveStack, Move *repeatMove, int *retMSP); // converts a long-algebraic text move to your internal move format
int ReadSquare(char *p, int *sqr);
#endif
