/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/

#include "eval.h"

signed char psq[PSTSIZE][BSIZE] = { 0 }; // cache of piece-value-per-square
