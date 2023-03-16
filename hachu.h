/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#ifndef HACHU_H
#define HACHU_H

#include "types.h"

#define VERSION "0.23"

// promotion codes
#define CAN_PROMOTE 0x11
#define DONT_DEFER  0x22
#define CANT_DEFER  0x44
#define LAST_RANK   0x88
#define P_WHITE     0x0F
#define P_BLACK     0xF0

void pmap(Color color);
void pboard(int *b);
void pbytes(Flag *b);
int myRandom();

// Some routines your engine should have to do the various essential things
Color MakeMove2(Color stm, Move move); // performs move, and returns new side to move
Flag InCheck(Color stm, int level); // generates attack maps, and determines if king/prince is in check
void UnMake2(Move move);            // unmakes the move;
Color Setup2(char *fen);            // sets up the position from the given FEN, and returns the new side to move
int ListMoves(Color stm, int listStart, int listEnd);
void SetMemorySize(int n);          // if n is different from last time, resize all tables to make memory usage below n MB
int SearchBestMove(Color stm, Move *move, Move *ponderMove, int msp);
#endif
