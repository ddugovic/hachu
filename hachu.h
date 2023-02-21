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
#    include <windows.h>
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
#    include <sys/time.h>
#    include <sys/ioctl.h>
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

// Piece-Square Tables
#define PST_NEUTRAL 0
#define PST_STEPPER BW
#define PST_WJUMPER (BSIZE)
#define PST_SLIDER  (BSIZE+BW)
#define PST_TRAP    (2*BSIZE)
#define PST_CENTER  (2*BSIZE+BW)
#define PST_WPPROM  (3*BSIZE)
#define PST_BPPROM  (3*BSIZE+BW)
#define PST_BJUMPER (4*BSIZE)
#define PST_ZONDIST (4*BSIZE+BW)
#define PST_ADVANCE (5*BSIZE)
#define PST_RETRACT (5*BSIZE+BW)
#define PST_WFLYER  (6*BSIZE)
#define PST_BFLYER  (6*BSIZE+BW)
#define PST_LANCE   (7*BSIZE)
#define PSTSIZE     (8*BSIZE)

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
char fenArray[4000], startPos[4000], *reason, checkStack[300];
int retFirst, retMSP, retDep, pvPtr;
int nodes, startTime, lastRootMove, lastRootIter, tlim1, tlim2, tlim3, repCnt, comp;
Move ponderMove;
#define FIFTY 50
#define LEVELS 200
Move retMove, moveStack[20000], path[100], repStack[LEVELS+(FIFTY*2)], pv[1000], repeatMove[300], killer[100][2];

int maxDepth;                            // used by search

// Some global variables that control your engine's behavior
int ponder;
int randomize;
int postThinking;
int noCut=1;        // engine-defined option
int resign;         // engine-defined option
int contemptFactor; // likewise
int seed;
#endif
