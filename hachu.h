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

#define VERSION "0.21"

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
     {	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec*1000 + t.tv_usec/1000;
     }
#endif

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define BH 16
#define BW 16
#define BSIZE BW*BH
#define STEP(X,Y) (BW*(X)+ (Y))
#define POS(X,Y) STEP((BH-bRanks)/2 + X, (BW-bFiles)/2 + Y)
#define LL POS(0, 0)
#define LR POS(0, bFiles-1)
#define UL POS(bRanks-1, 0)
#define UR POS(bRanks-1, bFiles-1)
#define FILECH(s) ((s-LL)%BW+'a')
#define RANK(s) ((s-LL)/BW+1)

#define BLACK      0
#define WHITE      1
#define COLORS     2
#define SQLEN      8               /* bits in square number   */
#define EMPTY      0
#define EDGE   (1<<SQLEN)
#define TYPE   (WHITE|BLACK|EDGE)
#define ABSENT  2047
#define INF     8000
#define NPIECES EDGE+1             /* length of piece list    */

#define SQUARE  ((1<<SQLEN) - 1)   /* mask for square in move */
#define DEFER   (1<<2*SQLEN)       /* deferral on zone entry  */
#define PROMOTE (1<<2*SQLEN+1)     /* promotion bit in move   */
#define INVALID     0              /* cannot occur as a valid move */
#define SPECIAL  1400              /* start of special moves  */
#define BURN    (SPECIAL + 96)     /* start of burn encodings */
#define CASTLE  (SPECIAL + 100)    /* castling encodings      */
#define SORTKEY(X) 0

// promotion codes
#define CAN_PROMOTE 0x11
#define DONT_DEFER  0x22
#define CANT_DEFER  0x44
#define LAST_RANK   0x88
#define P_WHITE     0x0F
#define P_BLACK     0xF0

char *MoveToText(Move move, int m);     // from WB driver
void pmap(int color);
void pboard(int *b);
void pbytes(Flag *b);
int myRandom();

HashBucket *hashTable;
int hashMask;

#define H_UPPER 2
#define H_LOWER 1

Flag abortFlag;
char fenArray[4000], startPos[4000], *reason;
int retFirst, retMSP, retDep, pvPtr;
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
int  MakeMove2(int stm, Move move); // performs move, and returns new side to move
Flag InCheck(int level);            // generates attack maps, and determines if king/prince is in check
void UnMake2(Move move);            // unmakes the move;
int  Setup2(char *fen);             // sets up the position from the given FEN, and returns the new side to move
void SetMemorySize(int n);          // if n is different from last time, resize all tables to make memory usage below n MB
char *MoveToText(Move move, int m); // converts the move from your internal format to text like e2e2, e1g1, a7a8q.
Move ParseMove(int ls, int le, char *moveText); // converts a long-algebraic text move to your internal move format
int  SearchBestMove(Move *move, Move *ponderMove);
void PonderUntilInput(int stm);     // Search current position for stm, deepening forever until there is input.
