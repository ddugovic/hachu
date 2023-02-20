/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/

#include <time.h>
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

char *array, *IDs, fenArray[4000], startPos[4000], *reason, checkStack[300];
int bFiles, bRanks, zone, currentVariant, chuFlag, tenFlag, chessFlag, repDraws, stalemate;
int tsume, pvCuts, allowRep, entryProm=1, okazaki, pVal;
int stm, xstm, hashKeyH=1, hashKeyL=1, framePtr, msp, nonCapts, rootEval, filling, promoDelta;
int retFirst, retMSP, retDep, pvPtr, level, cnt50, mobilityScore;
int nodes, startTime, lastRootMove, lastRootIter, tlim1, tlim2, tlim3, repCnt, comp, abortFlag;
Move ponderMove;
#define FIFTY 50
#define LEVELS 200
Move retMove, moveStack[20000], path[100], repStack[LEVELS+(FIFTY*2)], pv[1000], repeatMove[300], killer[100][2];

      int maxDepth;                            // used by search

Vector direction[] = { // clockwise!
  {1,  0},
  {1,  1},
  {0,  1},
  {-1, 1},
  {-1, 0},
  {-1,-1},
  {0, -1},
  {1, -1},

  { 2, 1}, // Knight jumps
  { 1, 2},
  {-1, 2},
  {-2, 1},
  {-2,-1},
  {-1,-2},
  { 1,-2},
  { 2,-1}
};

int epList[104], ep2List[104], toList[104], reverse[104];  // decoding tables for double and triple moves
int kingStep[10], knightStep[10];         // raw tables for step vectors (indexed as -1 .. 8)
int neighbors[9];   // similar to kingStep, but starts with null-step
char fireFlags[10]; // flags for Fire-Demon presence (last two are dummies, which stay 0, for compactify)
#define kStep (kingStep+1)
#define nStep (knightStep+1)

int attackMask[RAYS] = { // indicate which bits in attack-map item are used to count attacks from which direction
  000000007,
  000000070,
  000000700,
  000007000,
  000070000,
  000700000,
  007000000,
  070000000
};

int rayMask[RAYS] = { // indicate which bits in attack-map item are used to count attacks from which direction
  000000077,
  000000077,
  000007700,
  000007700,
  000770000,
  000770000,
  077000000,
  077000000
};

int ray[RAYS+1] = { // 1 in the bit fields for the various directions
  000000001,
  000000010,
  000000100,
  000001000,
  000010000,
  000100000,
  001000000,
  010000000,
 0100000000  // marks knight jumps
};

//                                           Main Data structures
//
// Piece List:
//   Interleaved lists for black and white, white taking the odd slots
//   Pieces in general have two entries: one for the basic, and one for the promoted version
//   The unused one gets its position set to the invalid square number ABSENT
//   The list is sorted by piece value, most valuable pieces first
//   When a piece is captured in the search, both its versions are marked ABSENT
//   In the root the list is packed to eliminate all captured pieces
//   The list contains a table for how far the piece moves in each direction
//   Range encoding: -3 = full Lion, -2 = on-ray Lion, -1 = plain jump, 0 = none, 1 = step, >1 = (limited) range

// Attack table:
//   A table in board format, containing pairs of consecutive integers for each square (indexed as 2*sqr and 2*sqr+1)
//   The first integer contains info on black attacks to the square, the second similarly for white attacks
//   Each integer contains eight 3-bit fields, which count the number of attacks on it with moves in a particular direction
//   (If there are attacks by range-jumpers, the 3-bit count is increased by 2 over the actual value)

// Board:
//   The board has twice as many files as actually used, in 0x88 fashion
//   The used squares hold the piece numbers (for use as index in the piece list)
//   Unused squares are set to the invalid piece number EDGE
//   There are also 3 guard ranks of EDGE on each side

// Moves:
//   Moves are encoded as 11-bit from-square and to-square numbers packed in the low bits of an int
//   Special moves (like Lion double moves) are encoded by off-board to-squares above a certain value
//   Promotions are indicated by bit 22

// Hash table:
//   Entries of 16 bytes, holding a 32-bit signature, 16-bit lower- and upper-bound scores,
//   8-bit draft of each of those scores, an age counter that stores the search number of the last access.
//   The hash key is derived as the XOR of the products pieceKey[piece]*squareKey[square].

// Promotion zones:
//   the promoBoard contains one byte with flags for each square, to indicate for each side whether the square
//   is in the promotion zone (twice), on the last two ranks, or on the last rank
//   the promoFlag field in the piece list can select which bits of this are tested, to see if it
//   (1) can promote (2) can defer when the to-square is on last rank, last two ranks, or anywhere.
//   Pawns normally can't defer anywhere, but if the user defers with them, their promoFlag is set to promote on last rank only

int pieces[COLORS], royal[COLORS], kylin[COLORS];
PieceInfo p[NPIECES]; // piece list

    // Some global variables that control your engine's behavior
    int ponder;
    int randomize;
    int postThinking;
    int noCut=1;        // engine-defined option
    int resign;         // engine-defined option
    int contemptFactor; // likewise
    int seed;

int squareKey[BSIZE];

int board[BSIZE] = { [0 ... BSIZE-1] = EDGE };
// Stockfish and many engines compute attacks on demand using magic bitboards
// whereas HaChu attempts in vain to maintain "this square attacks these rays"
// further compounded by "level" (search depth).
// There isn't a simple algorithm to generate and cache discovered attacks;
// therefore, attacksByLevel is recalculated for all pieces for both players
// after every move.  Please do not attempt to implement complex algorithms...
// it's not worth the complexity or effort, unless performance is critical
// and you are certain magic bitboards are inadequate.
// The question of "what squares does my piece attack?" could benefit from a map
// of squares to PieceDesc (or range) however "is a square attacked?" might not.
int attacksByLevel[LEVELS][COLORS][BSIZE];
#define attacks attacksByLevel[level]
#define ATTACK(pos, color) attacks[color][pos]
char promoBoard[BSIZE] = { [0 ... BSIZE-1] = 0 }; // flags to indicate promotion zones
Flag fireBoard[BSIZE]; // flags to indicate squares controlled by Fire Demons
signed char PST[PSTSIZE] = { 0 };

// Maximum of (ranks, files) of ray between squares
#define dist(s1, s2) MAX(abs((s1-s2)/BW), abs((s1-s2)%BW))
