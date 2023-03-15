/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#ifndef HACHU_H
#define HACHU_H

#include <time.h>
#include "types.h"

#define VERSION "0.23"

#ifdef WIN32
#include <windows.h>
     int InputWaiting()
     {  // checks for waiting input in pipe
        static int pipe, init;
        static HANDLE inp;
        DWORD cnt;

        if(!init) inp = GetStdHandle(STD_INPUT_HANDLE);
        if(!PeekNamedPipe(inp, NULL, 0, NULL, &cnt, NULL)) return 1;
        return cnt;
    }
#else
#include <sys/time.h>
#include <sys/ioctl.h>
     int InputWaiting()
     {
        int cnt;
        if(ioctl(0, FIONREAD, &cnt)) return 1;
        return cnt;
     }
     int GetTickCount() // with thanks to Tord
     {
        struct timeval t;
        gettimeofday(&t, NULL);
        return t.tv_sec*1000 + t.tv_usec/1000;
     }
#endif

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
void SetMemorySize(int n);          // if n is different from last time, resize all tables to make memory usage below n MB
Move ParseMove(Color stm, int ls, int le, char *moveText); // converts a long-algebraic text move to your internal move format
int SearchBestMove(Color stm, Move *move, Move *ponderMove, int msp);
#endif
