/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#ifndef HACHU_H
#define HACHU_H

#include <time.h>
#include "piece.h"
#include "types.h"

#define VERSION "0.22"

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

HashBucket *hashTable;
int hashMask;

#define H_UPPER 2
#define H_LOWER 1

char abortFlag, fenArray[4000], startPos[4000], *reason;
int nonCapts, retFirst, retMSP, retDep, pvPtr;
int nodes, startTime, lastRootMove, lastRootIter, tlim1, tlim2, tlim3, repCnt, comp;
Move ponderMove;
#define FIFTY 50
#define LEVELS 200
Move retMove, moveStack[20000], path[100], repStack[LEVELS+(FIFTY*2)], pv[1000], repeatMove[300], killer[100][2];
Flag checkStack[LEVELS+(FIFTY*2)];

int level, maxDepth, mobilityScore; // used by search

// Some global variables that control your engine's behavior
int ponder;
int randomize;
int postThinking;
int noCut=1;        // engine-defined option
int resign;         // engine-defined option
int contemptFactor; // likewise
int seed;
int tsume, pvCuts, allowRep, entryProm=1, okazaki;
#endif

// Some routines your engine should have to do the various essential things
Color MakeMove2(Color stm, Move move); // performs move, and returns new side to move
Flag InCheck(Color stm, int level); // generates attack maps, and determines if king/prince is in check
void UnMake2(Move move);            // unmakes the move;
Color Setup2(char *fen);            // sets up the position from the given FEN, and returns the new side to move
void SetMemorySize(int n);          // if n is different from last time, resize all tables to make memory usage below n MB
MoveInfo MoveToInfo(Move move);     // unboxes (from, to, path)
char *MoveToText(Move move, int m); // converts the move from your internal format to text like e2e2, e1g1, a7a8q.
Move ParseMove(Color stm, int ls, int le, char *moveText); // converts a long-algebraic text move to your internal move format
int SearchBestMove(Color stm, Move *move, Move *ponderMove, int msp);
