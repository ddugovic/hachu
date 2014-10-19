/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/

// TODO:
// in GenCapts we do not generate jumps of more than two squares yet
// promotions by pieces with Lion power stepping in & out the zone in same turn
// promotion on capture

#define VERSION "0.20"

//define PATH level==0 || path[0] == 0x590cb &&  (level==1 || path[1] == 0x4c0c9 && (level == 2 || path[2] == 0x8598ca && (level == 3 /*|| path[3] == 0x3e865 && (level == 4 || path[4] == 0x4b865 && (level == 5))*/)))
#define PATH 0

#define HASH
#define KILLERS
#define NULLMOVE
#define CHECKEXT
#define LMR 4
#define LIONTRAP
#define XWINGS
#define KINGSAFETY
#define KSHIELD
#define FORTRESS
#define PAWNBLOCK
#define TANDEM 100 /* bonus for pairs of attacking light steppers */
#define KYLIN 100 /* extra end-game value of Kylin for promotability */
#define PROMO 0 /* extra bonus for 'vertical' piece when it actually promotes (diagonal pieces get half) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>

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

#define BW bWidth
#define BH bHeight
#define BHMAX 16
#define BWMAX (2*BHMAX)
#define BSIZE BWMAX*BHMAX
#define ZONE  zone

#define ONE 1 /* currently no variants with 10-deep board */

#define BLACK      0
#define WHITE      1
#define EMPTY      0
#define EDGE   (1<<11)
#define TYPE   (WHITE|BLACK|EDGE)
#define ABSENT  2047
#define INF     8000
#define NPIECES EDGE+1               /* length of piece list    */

#define SQLEN    11                /* bits in square number   */
#define SQUARE  ((1<<SQLEN) - 1)   /* mask for square in move */
#define DEFER   (1<<2*SQLEN)       /* deferral on zone entry  */
#define PROMOTE (1<<2*SQLEN+1)     /* promotion bit in move   */
#define SPECIAL  1400              /* start of special moves  */
#define BURN    (SPECIAL + 96)     /* start of burn encodings */
#define CASTLE  (SPECIAL + 100)    /* castling encodings      */
#define STEP(X,Y) (BW*(X)+ (Y))
#define SORTKEY(X) 0

#define UPDATE_MOBILITY(X,Y)
#define ADDMOVE(X,Y)

// promotion codes
#define CAN_PROMOTE 0x11
#define DONT_DEFER  0x22
#define CANT_DEFER  0x44
#define LAST_RANK   0x88
#define P_WHITE     0x0F
#define P_BLACK     0xF0

// Piece-Square Tables
#define PST_NEUTRAL 0
#define PST_STEPPER BH
#define PST_WJUMPER (BW*BH)
#define PST_SLIDER  (BW*BH+BH)
#define PST_TRAP    (2*BW*BH)
#define PST_CENTER  (2*BW*BH+BH)
#define PST_WPPROM  (3*BW*BH)
#define PST_BPPROM  (3*BW*BH+BH)
#define PST_BJUMPER (4*BW*BH)
#define PST_ZONDIST (4*BW*BH+BH)
#define PST_ADVANCE (5*BW*BH)
#define PST_RETRACT (5*BW*BH+BH)
#define PST_WFLYER  (6*BW*BH)
#define PST_BFLYER  (6*BW*BH+BH)
#define PST_LANCE   (7*BW*BH)
#define PST_END     (8*BW*BH)

typedef unsigned int Move;

char *MoveToText(Move move, int m);     // from WB driver
void pmap(int *m, int col);
void pboard(int *b);
void pbytes(unsigned char *b);

typedef struct {
  int lock[5];
  int move[5];
  short int score[5];
  char depth[5];
  char flag[5];
  char age[4];
} HashBucket;

HashBucket *hashTable;
int hashMask;

#define H_UPPER 2
#define H_LOWER 1

typedef struct {
  char *name, *promoted;
  int value;
  signed char range[8];
  char bulk;
  char ranking;
  int whiteKey, blackKey;
} PieceDesc;

typedef struct {
  int from, to, piece, victim, new, booty, epSquare, epVictim[9], ep2Square, revMoveCount;
  int savKeyL, savKeyH, gain, loss, filling, saveDelta;
  char fireMask;
} UndoInfo;

char *array, fenArray[4000], startPos[4000], *reason, checkStack[300];
int bWidth, bHeight, bsize, zone, currentVariant, chuFlag, tenFlag, chessFlag, repDraws, stalemate;
int tsume, pvCuts, allowRep, entryProm, okazaki, pVal;
int stm, xstm, hashKeyH=1, hashKeyL=1, framePtr, msp, nonCapts, rootEval, filling, promoDelta;
int retMSP, retFirst, retDep, pvPtr, level, cnt50, mobilityScore;
int ll, lr, ul, ur; // corner squares
int nodes, startTime, lastRootMove, lastRootIter, tlim1, tlim2, tlim3, repCnt, comp, abortFlag;
Move ponderMove;
Move retMove, moveStack[20000], path[100], repStack[300], pv[1000], repeatMove[300], killer[100][2];

      int maxDepth;                            // used by search

#define X 36 /* slider              */
#define R 37 /* jump capture        */
#define N -1 /* Knight              */
#define J -2 /* jump                */
#define D -3 /* linear double move  */
#define T -4 /* linear triple move  */
#define L -5 /* true Lion move      */
#define F -6 /* Lion + 3-step       */
#define S -7 /* Lion + range        */
#define H -9 /* hook move           */
#define C -10 /* capture only       */
#define M -11 /* non-capture only   */

#define LVAL 1000 /* piece value of Lion. Used in chu for recognizing it to implement Lion-trade rules  */
#define FVAL 5000 /* piece value of Fire Demon. Used in code for recognizing moves with it and do burns */

PieceDesc chuPieces[] = {
  {"LN", "",  LVAL, { L,L,L,L,L,L,L,L }, 4 }, // lion
  {"FK", "",   600, { X,X,X,X,X,X,X,X }, 4 }, // free king
  {"SE", "",   550, { X,D,X,X,X,X,X,D }, 4 }, // soaring eagle
  {"HF", "",   500, { D,X,X,X,X,X,X,X }, 4 }, // horned falcon
  {"FO", "",   400, { X,X,0,X,X,X,0,X }, 4 }, // flying ox
  {"FB", "",   400, { 0,X,X,X,0,X,X,X }, 4 }, // free boar
  {"DK", "SE", 400, { X,1,X,1,X,1,X,1 }, 4 }, // dragon king
  {"DH", "HF", 350, { 1,X,1,X,1,X,1,X }, 4 }, // dragon horse
  {"WH", "",   350, { X,X,0,0,X,0,0,X }, 3 }, // white horse
  {"R",  "DK", 300, { X,0,X,0,X,0,X,0 }, 4 }, // rook
  {"FS", "",   300, { X,1,1,1,X,1,1,1 }, 3 }, // flying stag
  {"WL", "",   250, { X,0,0,X,X,X,0,0 }, 4 }, // whale
  {"K",  "",   280, { 1,1,1,1,1,1,1,1 }, 2, 4 }, // king
  {"CP", "",   270, { 1,1,1,1,1,1,1,1 }, 2, 4 }, // king
  {"B",  "DH", 250, { 0,X,0,X,0,X,0,X }, 2 }, // bishop
  {"VM", "FO", 200, { X,0,1,0,X,0,1,0 }, 2 }, // vertical mover
  {"SM", "FB", 200, { 1,0,X,0,1,0,X,0 }, 6 }, // side mover
  {"DE", "CP", 201, { 1,1,1,1,0,1,1,1 }, 2 }, // drunk elephant
  {"BT", "FS", 152, { 0,1,1,1,1,1,1,1 }, 2 }, // blind tiger
  {"G",  "R",  151, { 1,1,1,0,1,0,1,1 }, 2 }, // gold
  {"FL", "B",  150, { 1,1,0,1,1,1,0,1 }, 2 }, // ferocious leopard
  {"KN", "LN", 154, { J,1,J,1,J,1,J,1 }, 2 }, // kirin
  {"PH", "FK", 153, { 1,J,1,J,1,J,1,J }, 2 }, // phoenix
  {"RV", "WL", 150, { X,0,0,0,X,0,0,0 }, 1 }, // reverse chariot
  {"L",  "WH", 150, { X,0,0,0,0,0,0,0 }, 1 }, // lance
  {"S",  "VM", 100, { 1,1,0,1,0,1,0,1 }, 2 }, // silver
  {"C",  "SM", 100, { 1,1,0,0,1,0,0,1 }, 2 }, // copper
  {"GB", "DE",  50, { 1,0,0,0,1,0,0,0 }, 1 }, // go between
  {"P",  "G",   40, { 1,0,0,0,0,0,0,0 }, 2 }, // pawn
  { NULL }  // sentinel
};

PieceDesc shoPieces[] = {
  {"DK", "",   700, { X,1,X,1,X,1,X,1 } }, // dragon king
  {"DH", "",   520, { 1,X,1,X,1,X,1,X } }, // dragon horse
  {"R",  "DK", 500, { X,0,X,0,X,0,X,0 } }, // rook
  {"B",  "DH", 320, { 0,X,0,X,0,X,0,X } }, // bishop
  {"K",  "",   410, { 1,1,1,1,1,1,1,1 } }, // king
  {"CP", "",   400, { 1,1,1,1,1,1,1,1 } }, // king
  {"DE", "CP", 250, { 1,1,1,1,0,1,1,1 } }, // silver
  {"G",  "",   220, { 1,1,1,0,1,0,1,1 } }, // gold
  {"S",  "G",  200, { 1,1,0,1,0,1,0,1 } }, // silver
  {"L",  "G",  150, { X,0,0,0,0,0,0,0 } }, // lance
  {"N",  "G",  110, { N,0,0,0,0,0,0,N } }, // Knight
  {"P",  "G",   80, { 1,0,0,0,0,0,0,0 } }, // pawn
  { NULL }  // sentinel
};

PieceDesc daiPieces[] = {
  {"FD", "G", 150, { 0,2,0,2,0,2,0,2 }, 2 }, // Flying Dragon
  {"VO", "G", 200, { 2,0,2,0,2,0,2,0 }, 2 }, // Violent Ox
  {"EW", "G",  80, { 1,1,1,0,0,0,1,1 }, 2 }, // Evil Wolf
  {"CS", "G",  70, { 0,1,0,1,0,1,0,1 }, 1 }, // Cat Sword
  {"AB", "G",  60, { 1,0,1,0,1,0,1,0 }, 1 }, // Angry Boar
  {"I",  "G",  80, { 1,1,0,0,0,0,0,1 }, 2 }, // Iron
  {"N",  "G",  60, { N,0,0,0,0,0,0,N }, 0 }, // Knight
  {"ST", "G",  50, { 0,1,0,0,0,0,0,1 }, 0 }, // Stone
  { NULL }  // sentinel
};

PieceDesc ddPieces[] = {
  {"LO", "",   10, { 1,H,1,H,1,H,1,H } }, // Long-Nosed Goblin
  {"OK", "LO", 10, { 2,1,2,0,2,0,2,1 } }, // Old Kite
  {"PS", "HM", 10, { J,0,1,J,0,J,1,0 } }, // Poisonous Snake
  {"GE", "",   10, { 3,3,5,5,3,5,5,3 } }, // Great Elephant
  {"WS", "LD", 10, { 1,1,2,0,1,0,2,1 } }, // Western Barbarian
  {"EA", "LN", 10, { 2,1,1,0,2,0,1,1 } }, // Eastern Barbarian
  {"NO", "FE", 10, { 0,2,1,1,0,1,1,2 } }, // Northern Barbarian
  {"SO", "WE", 10, { 0,1,1,2,0,2,1,1 } }, // Southern Barbarian
  {"FE", "",   10, { 2,X,2,2,2,2,2,X } }, // Fragrant Elephant
  {"WE", "",   10, { 2,2,2,X,2,X,2,2 } }, // White Elephant
  {"FT", "",   10, { X,X,5,0,X,0,5,X } }, // Free Dream-Eater
  {"FR", "",   10, { 5,X,X,0,5,0,X,X } }, // Free Demon
  {"WB", "FT", 10, { 2,X,X,X,2,X,X,X } }, // Water Buffalo
  {"RB", "FR", 10, { X,X,X,X,0,X,X,X } }, // Rushing Bird
  {"SB", "",   10, { X,X,2,2,2,2,2,X } }, // Standard Bearer

  {"FH", "FK", 10, { 1,2,1,0,1,0,1,2 } }, // Flying Horse
  {"NK", "SB", 10, { 1,1,1,1,1,1,1,1 } }, // Neighbor King
  {"BM", "MW", 10, { 0,1,1,1,0,1,1,1 } }, // Blind Monkey
  {"DO", "",   10, { 2,X,2,X,2,X,2,X } }, // Dove
  {"EB", "DO", 10, { 2,0,2,0,0,0,2,0 } }, // Enchanted Badger
  {"EF", "SD", 10, { 0,2,0,0,2,0,0,2 } }, // Enchanted Fox
  {"RA", "",   10, { X,0,X,1,X,1,X,0 } }, // Racing Chariot
  {"SQ", "",   10, { X,1,X,0,X,0,X,1 } }, // Square Mover
  {"PR", "SQ", 10, { 1,1,2,1,0,1,2,1 } }, // Prancing Stag
  {"WT", "",   10, { X,1,2,0,X,0,2,X } }, // White Tiger
  {"BD", "",   10, { 2,X,X,0,2,0,X,1 } }, // Blue Dragon
  {"HD", "",   10, { X,0,0,0,1,0,0,0 } }, // Howling Dog
  {"VB", "",   10, { 0,2,1,0,0,0,1,2 } }, // Violent Bear
  {"SA", "",   10, { 2,1,0,0,2,0,0,1 } }, // Savage Tiger
  {"W",  "",   10, { 0,2,0,0,0,0,0,2 } }, // Wood
  {"CS", "DH",  70, { 0,1,0,1,0,1,0,1 } }, // cat sword
  {"FD", "DK", 150, { 0,2,0,2,0,2,0,2 } }, // flying dragon
  {"KN", "GD", 150, { J,1,J,1,J,1,J,1 } }, // kirin
  {"PH", "GB", 150, { 1,J,1,J,1,J,1,J } }, // phoenix
  {"LN", "FF",  1000, { L,L,L,L,L,L,L,L } }, // lion
  {"LD", "GE", 10, { T,T,T,T,T,T,T,T } }, // Lion Dog
  {"AB", "", 10, { 1,0,1,0,1,0,1,0 } }, // Angry Boar
  {"B",  "", 10, { 0,X,0,X,0,X,0,X } }, // Bishop
  {"C",  "", 10, { 1,1,0,0,1,0,0,1 } }, // Copper
  {"DH", "", 10, { 1,X,1,X,1,X,1,X } }, // Dragon Horse
  {"DK", "", 10, { X,1,X,1,X,1,X,1 } }, // Dragon King
  {"FK", "", 10, {  } }, // 
  {"EW", "", 10, { 1,1,1,0,0,0,1,1 } }, // Evil Wolf
  {"FL", "", 10, {  } }, // 
  {"", "", 10, {  } }, // 
  {"", "", 10, {  } }, // 
  {"", "", 10, {  } }, // 
  {"", "", 10, {  } }, // 
  { NULL }  // sentinel
};

PieceDesc makaPieces[] = {
  {"DV", "", 10, { 0,1,0,1,0,0,1,1 } }, // Deva
  {"DS", "", 10, { 0,1,1,0,0,1,0,1 } }, // Dark Spirit
  {"T",  "", 10, { 0,1,0,0,1,0,0,1 } }, // Tile
  {"CS", "", 10, { 1,0,0,1,1,1,0,0 } }, // Coiled Serpent
  {"RD", "", 10, { 1,0,1,1,1,1,1,0 } }, // Reclining Dragon
  {"CC", "", 10, { 0,1,1,0,1,0,1,1 } }, // Chinese Cock
  {"OM", "", 10, { 0,1,0,1,1,1,0,1 } }, // Old Monkey
  {"BB", "", 10, { 0,1,0,1,X,1,0,1 } }, // Blind Bear
  {"OR", "", 10, { 0,2,0,0,2,0,0,2 } }, // Old Rat
  {"LD", "WS", 10, { T,T,T,T,T,T,T,T } }, // Lion Dog
  {"WR", "", 10, { 0,3,1,3,0,3,1,3 } }, // Wrestler
  {"GG", "", 10, { 3,1,3,0,3,0,3,1 } }, // Guardian of the Gods
  {"BD", "", 10, { 0,3,1,0,1,0,1,3 } }, // Budhist Devil
  {"SD", "", 10, { 5,2,5,2,5,2,5,2 } }, // She-Devil
  {"DY", "", 10, { J,0,1,0,J,0,1,0 } }, // Donkey
  {"CP", "", 10, { 0,H,0,2,0,2,0,H } }, // Capricorn
  {"HM", "", 10, { H,0,H,0,H,0,H,0 } }, // Hook Mover
  {"SF", "", 10, { 0,1,X,1,0,1,0,1 } }, // Side Flier
  {"LC", "", 10, { X,0,0,X,1,0,0,X } }, // Left Chariot
  {"RC", "", 10, { X,X,0,0,1,X,0,0 } }, // Right Chariot
  {"FG", "", 10, { X,X,X,0,X,0,X,X } }, // Free Gold
  {"FS", "", 10, { X,X,0,X,0,X,0,X } }, // Free Silver
  {"FC", "", 10, { X,X,0,0,X,0,0,X } }, // Free Copper
  {"FI", "", 10, { X,X,0,0,0,0,0,X } }, // Free Iron
  {"FT", "", 10, { 0,X,0,0,X,0,0,X } }, // Free Tile
  {"FN", "", 10, { 0,X,0,0,0,0,0,X } }, // Free Stone
  {"FTg", "", 10, { 0,X,X,X,X,X,X,X } }, // Free Tiger
  {"FLp", "", 10, { X,X,0,X,X,X,0,X } }, // Free Leopard (Free Boar?)
  {"FSp", "", 10, { X,0,0,X,X,X,0,0 } }, // Free Serpent (Whale?)
  {"FrD", "", 10, { X,0,X,X,X,X,X,0 } }, // Free Dragon
  {"FC", "", 10, { 0,X,0,X,0,X,0,X } }, // Free Cat (Bishop?)
  {"EM", "", 10, {  } }, // Emperor
  {"TK", "", 10, {  } }, // Teaching King
  {"BS", "", 10, {  } }, // Budhist Spirit
  {"WS", "", 10, { X,X,0,X,1,X,0,X } }, // Wizard Stork
  {"MW", "", 10, { 1,X,0,X,X,X,0,X } }, // Mountain Witch
  {"FF", "", 10, {  } }, // Furious Fiend
  {"GD", "", 10, { 2,3,X,3,2,3,X,3 } }, // Great Dragon
  {"GB", "", 10, { X,3,2,3,X,3,2,3 } }, // Golden Bird
  {"FrW", "", 10, {  } }, // Free Wolf
  {"FrB", "", 10, {  } }, // Free Bear
  {"BT", "", 10, { X,0,0,X,0,X,0,0 } }, // Bat
  {"", "", 10, {  } }, // 
  { NULL }  // sentinel
};

PieceDesc taiPieces[] = {
  {"", "", 10, {  } }, // Peacock
  {"", "", 10, {  } }, // Vermillion Sparrow
  {"", "", 10, {  } }, // Turtle Snake
  {"", "", 10, {  } }, // Silver Hare
  {"", "", 10, {  } }, // Golden Deer
  {"", "", 10, {  } }, // 
  {"", "", 10, {  } }, // 
  {"", "", 10, {  } }, // 
  { NULL }  // sentinel
};

PieceDesc tenjikuPieces[] = { // only those not in Chu, or different (because of different promotion)
  {"FI", "",  FVAL, { X,X,0,X,X,X,0,X } }, // Fire Demon
  {"GG", "",  1500, { R,R,R,R,R,R,R,R }, 0, 3 }, // Great General
  {"VG", "",  1400, { 0,R,0,R,0,R,0,R }, 0, 2 }, // Vice General
  {"RG", "GG",1200, { R,0,R,0,R,0,R,0 }, 0, 1 }, // Rook General
  {"BG", "VG",1100, { 0,R,0,R,0,R,0,R }, 0, 1 }, // Bishop General
  {"SE", "RG", 10, { X,D,X,X,X,X,X,D } }, // Soaring Eagle
  {"HF", "BG", 10, { D,X,X,X,X,X,X,X } }, // Horned Falcon
  {"LH", "",   10, { L,S,L,S,L,S,L,S } }, // Lion-Hawk
  {"LN", "LH",LVAL, { L,L,L,L,L,L,L,L } }, // Lion
  {"FE", "",   1,   { X,X,X,X,X,X,X,X } }, // Free Eagle
  {"FK", "FE", 600, { X,X,X,X,X,X,X,X } }, // Free King
  {"HT", "",   10, { X,X,2,X,X,X,2,X } }, // Heavenly Tetrarchs
  {"CS", "HT", 10, { X,X,2,X,X,X,2,X } }, // Chariot Soldier
  {"WB", "FI", 10, { 2,X,X,X,2,X,X,X } }, // Water Buffalo
  {"VS", "CS", 10, { X,0,2,0,1,0,2,0 } }, // Vertical Soldier
  {"SS", "WB", 10, { 2,0,X,0,1,0,X,0 } }, // Side Soldier
  {"I",  "VS", 10, { 1,1,0,0,0,0,0,1 } }, // Iron
  {"N",  "SS", 10, { N,0,0,0,0,0,0,N } }, // Knight
  {"MG", "",   10, { X,0,0,X,0,X,0,0 } }, // Multi-General
  {"D",  "MG", 10, { 1,0,0,1,0,1,0,0 } }, // Dog
  { NULL }  // sentinel
};

PieceDesc taikyokuPieces[] = {
  {"", "", 10, {  } }, // 
  { NULL }  // sentinel
};

PieceDesc chessPieces[] = {
  {"FK", "", 950, { X,X,X,X,X,X,X,X } },
  {"R", "",  500, { X,0,X,0,X,0,X,0 } },
  {"B", "",  320, { 0,X,0,X,0,X,0,X } },
  {"N", "",  300, { N,N,N,N,N,N,N,N } },
  {"K", "",  280, { 1,1,1,1,1,1,1,1 } },
  {"P", "FK", 80, { M,C,0,0,0,0,0,C } },
  { NULL }  // sentinel
};

PieceDesc lionPieces[] = {
  {"LN","", LVAL, { L,L,L,L,L,L,L,L } },
  {"FK", "", 600, { X,X,X,X,X,X,X,X } },
  {"R", "",  300, { X,0,X,0,X,0,X,0 } },
  {"K", "",  280, { 1,1,1,1,1,1,1,1 } },
  {"B", "",  190, { 0,X,0,X,0,X,0,X } },
  {"N", "",  180, { N,N,N,N,N,N,N,N } },
  {"P", "FK", 50, { M,C,0,0,0,0,0,C } },
  { NULL }  // sentinel
};

PieceDesc shatranjPieces[] = {
  {"FK", "", 150, { 0,1,0,1,0,1,0,1 } },
  {"R", "",  500, { X,0,X,0,X,0,X,0 } },
  {"B", "",   90, { 0,J,0,J,0,J,0,J } },
  {"N", "",  300, { N,N,N,N,N,N,N,N } },
  {"K", "",  280, { 1,1,1,1,1,1,1,1 } },
  {"P", "FK", 80, { M,C,0,0,0,0,0,C } },
  { NULL }  // sentinel
};

PieceDesc makrukPieces[] = {
  {"SM","",  150, { 0,1,0,1,0,1,0,1 } },
  {"R", "",  500, { X,0,X,0,X,0,X,0 } },
  {"S", "",  200, { 1,1,0,1,0,1,0,1 } }, // silver
  {"N", "",  300, { N,N,N,N,N,N,N,N } },
  {"K", "",  280, { 1,1,1,1,1,1,1,1 } },
  {"P", "SM", 80, { M,C,0,0,0,0,0,C } },
  { NULL }  // sentinel
};

char chuArray[] = "L:FLCSGK:DEGSC:FLL/:RV.B.:BT:KN:PH:BT.B.:RV/:SM:VMR:DH:DK:LN:FK:DK:DHR:VM:SM/PPPPPPPPPPPP/...:GB....:GB..."
		  "/............/............/"
		  "...:gb....:gb.../pppppppppppp/:sm:vmr:dh:dk:fk:ln:dk:dhr:vm:sm/:rv.b.:bt:ph:kn:bt.b.:rv/l:flcsg:dekgsc:fll";
char daiArray[] = "LN:STICSGKGSCI:STNL/:RV.:CS.:FL.:BT:DE:BT.:FL.:CS.:RV/.:VO.:AB.:EW:KN:LN:PH:EW.:AB.:VO./R:FD:SM:VMB:DH:DK:FK:DK:DHB:VM:SM:FDR"
		  "/PPPPPPPPPPPPPPP/....:GB.....:GB..../.............../.............../.............../....:gb.....:gb..../ppppppppppppppp/"
		  "r:fd:sm:vmb:dh:dk:fk:dk:dhb:vm:sm:fdr/.:vo.:ab.:ew:ph:ln:kn:ew.:ab.:vo./:rv.:cs.:fl.:bt:de:bt.:fl.:cs.:rv/ln:sticsgkgsci:stnl";
char tenArray[] = "LN:FLICSGK:DEGSCI:FLNL/:RV.:CS:CS.:BT:KN:LN:FK:PH:BT.:CS:CS.:RV/:SS:VSB:DH:DK:WB:FI:LH:FE:FI:WB:DK:DHB:VS:SS/"
		  ":SM:VMR:HF:SE:BG:RG:GG:VG:RG:BG:SE:HFR:VM:SM/PPPPPPPPPPPPPPPP/....D......D..../"
		  "................/................/................/................/"
		  "....d......d..../pppppppppppppppp/:sm:vmr:hf:se:bg:rg:vg:gg:rg:bg:se:hfr:vm:sm/"
		  ":ss:vsb:dh:dk:wb:fi:fe:lh:fi:wb:dk:dhb:vs:ss/:rv.:cs:cs.:bt:ph:fk:ln:kn:bt.:cs:cs.:rv/ln:flicsg:dekgsci:flnl";
char shoArray[] = "LNSGKGSNL/.B..:DE..R./PPPPPPPPP/........./........./........./ppppppppp/.r..:de..b./lnsgkgsnl";
char chessArray[] = "RNB:FKKBNR/PPPPPPPP/......../......../......../......../pppppppp/rnb:fkkbnr";
char lionArray[]  = "R:LNB:FKKBNR/PPPPPPPP/......../......../......../......../pppppppp/r:lnb:fkkbnr";
char shatArray[]= "RNBK:FKBNR/PPPPPPPP/......../......../......../......../pppppppp/rnbk:fkbnr";
char thaiArray[]= "RNSK:SMSNR/......../PPPPPPPP/......../......../pppppppp/......../rns:smksnr";

typedef struct {
  int boardWidth, boardFiles, boardRanks, zoneDepth, varNr; // board sizes
  char *name;  // WinBoard name
  char *array; // initial position
} VariantDesc;

typedef enum { V_CHESS, V_SHO, V_CHU, V_DAI, V_DADA, V_MAKA, V_TAI, V_KYOKU, V_TENJIKU, V_SHATRANJ, V_MAKRUK, V_LION } Variant;

#define SAME (-1)

VariantDesc variants[] = {
  { 24, 12, 12, 4, V_CHU,     "chu",     chuArray }, // Chu
  { 16,  8,  8, 1, V_CHESS,  "nocastle", chessArray }, // FIDE
  { 18,  9,  9, 3, V_SHO, "9x9+0_shogi", shoArray }, // Sho
  { 18,  9,  9, 3, V_SHO,     "sho",     shoArray }, // Sho duplicat
  { 30, 15, 15, 5, V_DAI,     "dai",     daiArray }, // Dai
  { 32, 16, 16, 5, V_TENJIKU, "tenjiku", tenArray }, // Tenjiku
  { 16,  8,  8, 1, V_SHATRANJ,"shatranj",shatArray}, // Shatranj
  { 16,  8,  8, 3, V_MAKRUK,  "makruk",  thaiArray}, // Makruk
  { 16,  8,  8, 1, V_LION,    "lion",    lionArray}, // Mighty Lion

  { 0, 0, 0, 0, 0 }, // sentinel
  { 34, 17, 17, 0, V_DADA,    "dada",    chuArray }, // Dai Dai
  { 38, 19, 19, 0, V_MAKA,    "maka",    chuArray }, // Maka Dai Dai
  { 50, 25, 25, 0, V_TAI,     "tai",     chuArray }, // Tai
  { 40, 36, 36, 0, V_KYOKU,   "kyoku",   chuArray }  // Taikyoku
};

typedef struct {
  int x, y;
} Vector;

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

int attackMask[8] = { // indicate which bits in attack-map item are used to count attacks from which direction
  000000007,
  000000070,
  000000700,
  000007000,
  000070000,
  000700000,
  007000000,
  070000000
};

int rayMask[8] = { // indicate which bits in attack-map item are used to count attacks from which direction
  000000077,
  000000077,
  000007700,
  000007700,
  000770000,
  000770000,
  077000000,
  077000000
};

int one[] = { // 1 in the bit fields for the various directions
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
// Promotion zones
//   the promoBoard contains one byte with flags for each square, to indicate for each side whether the square
//   is in the promotion zone (twice), on the last two ranks, or on the last rank
//   the promoFlag field in the piece list can select which bits of this are tested, to see if it
//   (1) can promote (2) can defer when the to-square is on last rank, last two ranks, or anywhere.
//   Pawns normally can't defer anywhere, but if the user defers with them, their promoFlag is set to promote on last rank only

typedef struct {
  int pos;
  int pieceKey;
  int promo;
  int value;
  int pst;
  signed char range[8];
  char promoFlag;
  char qval;
  char mobility;
  char mobWeight;
  unsigned char promoGain;
  char bulk;
} PieceInfo; // piece-list entry

int last[2], royal[2], kylin[2];
PieceInfo p[NPIECES]; // piece list

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

    // Some global variables that control your engine's behavior
    int ponder;
    int randomize;
    int postThinking;
    int noCut=1;        // engine-defined option
    int resign;         // engine-defined option
    int contemptFactor; // likewise
    int seed;

int squareKey[BSIZE];

int rawBoard[BSIZE + 11*BHMAX + 6];
//int attacks[2*BSIZE];   // attack map
int attackMaps[200*BSIZE], *attacks = attackMaps;
char distance[2*BSIZE]; // distance table
char promoBoard[BSIZE]; // flags to indicate promotion zones
char rawFire[BSIZE+2*BWMAX]; // flags to indicate squares controlled by Fire Demons
signed char PST[7*BSIZE];

#define board     (rawBoard + 6*BHMAX + 3)
#define fireBoard (rawFire + BWMAX + 1)
#define dist      (distance + BSIZE)

PieceDesc *
ListLookUp (char *name, PieceDesc *list)
{ // find piece of given name in list of descriptors
  while(list->name && strcmp(name, list->name)) list++;
  return (list->name == NULL ? NULL : list);
}

PieceDesc *
LookUp (char *name, int var)
{ // search piece of given name in all lists relevant for given variant
  PieceDesc *desc;
  switch(var) {
    case V_TENJIKU: // Tenjiku
      desc = ListLookUp(name, tenjikuPieces);
      if(desc) return desc;
      return ListLookUp(name, chuPieces);
    case V_SHO: // Sho
      return ListLookUp(name, shoPieces);
    case V_DAI: // Dai
      desc = ListLookUp(name, daiPieces);
      if(desc) return desc;
    case V_CHU: // Chu
      return ListLookUp(name, chuPieces);
    case V_CHESS: // FIDE
      return ListLookUp(name, chessPieces);
    case V_SHATRANJ: // Shatranj
      return ListLookUp(name, shatranjPieces);
    case V_MAKRUK: // Makruk
      return ListLookUp(name, makrukPieces);
    case V_LION: // Mighty Lion
      return ListLookUp(name, lionPieces);
  }
  return NULL;
}

void
SqueezeOut (int n)
{ // remove piece number n from the mentioned side's piece list (and adapt the reference to the displaced pieces!)
  int i;
  for(i=stm+2; i<=last[stm]; i+=2)
    if(p[i].promo > n) p[i].promo -= 2;
  for(i=n; i<last[stm]; i+=2) {
    p[i] = p[i+2];
    if(i+2 == royal[stm]) royal[stm] -= 2; // NB: squeezing out the King moves up Crown Prince to royal[stm]
    if(i+2 == kylin[stm]) kylin[stm] -= 2;
    if(i < 10) fireFlags[i-2] = fireFlags[i];
  }
  last[stm] -= 2;
}

int
Worse (int a, int b)
{ // determine if range a not upward compatible with b
  if(a == b) return 0;
  if(a >= 0 && b >= 0) return a < b;
  if(a >= 0) return 1; // a (limited) range can never do the same as a special move
  switch(a) {
    case J: return b < J; // any special move is better than a plain jump
    case D: return b > 2 || b < D;
    case T: return b > 3 || b < T;
    case L: return b > 2 || b < D;
    case F: return b > 3 || b < F || b == T;
    case S: return b == H || b == T;
    case H: return b < 0;
    default: return 1; // a >= 0, so b must be < 0 and can always do something a ranging move cannot do
  }
  return 0;
}

int
Lance (signed char *r)
{ // File-bound forward slider
  int i;
  for(i=1; i<4; i++) if(r[i] || r[i+4]) return 0;
  return r[0] == X;
}

int
EasyProm (signed char *r)
{
  int i;
  if(r[0] == X) return 30 + PROMO*((unsigned int)(r[1] | r[2] | r[3] | r[5] | r[6] | r[7]) <= 1);
  if(r[1] == X || r[7] == X) return 30 + PROMO/2;
  return 0;
}

int
IsUpwardCompatible (signed char *r, signed char *s)
{
  int i;
  for(i=0; i<8; i++) {
    if(Worse(r[i], s[i])) return 0;
  }
  return 1;
}

int
Forward (signed char *r)
{
  int i;
  for(i=2; i<7; i++) if(r[i]) return 0;
  return 1;
}

int
Range (signed char *r)
{
  int i, m=0;
  for(i=0; i<8; i++) {
    int d = r[i];
    if(r[i] < 0) d = r[i] >= L ? 2 : 36;
    if(d > m) m = d;
  }
  return m;
}

int multis[2], multiMovers[100];

void
StackMultis (int col)
{
  int i, j;
  multis[col] = col;
  for(i=col+2; i<=last[col]; i+=2) { // scan piece list for multi-capturers
    for(j=0; j<8; j++) if(p[i].range[j] < J && p[i].range[j] >= S || p[i].value == FVAL) {
      multiMovers[multis[col]] = i; // found one: put its piece number in list
      multis[col] += 2;
      break;
    }
  }
}

void
Compactify (int stm)
{ // remove pieces that are permanently gone (captured or promoted) from one side's piece list
  int i, j, k;
  for(i=stm+2; i<=last[stm]; i+=2) { // first pass: unpromoted pieces
    if((k = p[i].promo) >= 0 && p[i].pos == ABSENT) { // unpromoted piece no longer there
      p[k].promo = -2; // orphan promoted version
      SqueezeOut(i);
    }
  }
  for(i=stm+2; i<=last[stm]; i+=2) { // second pass: promoted pieces

    if((k = p[i].promo) == -2 && p[i].pos == ABSENT) { // orphaned promoted piece not present
      SqueezeOut(i);
    }
  }
  StackMultis(stm);
}

int
AddPiece (int stm, PieceDesc *list)
{
  int i, j, *key, v;
  for(i=stm+2; i<=last[stm]; i += 2) {
    if(p[i].value < list->value || p[i].value == list->value && (p[i].promo < 0)) break;
  }
  last[stm] += 2;
  for(j=last[stm]; j>i; j-= 2) p[j] = p[j-2];
  p[i].value = v = list->value;
  for(j=0; j<8; j++) p[i].range[j] = list->range[j^4*(WHITE-stm)];
  switch(Range(p[i].range)) {
    case 1:  p[i].pst = PST_STEPPER; break;
    case 2:  p[i].pst = PST_WJUMPER; break;
    default: p[i].pst = PST_SLIDER;  break;
  }
  key = (stm == WHITE ? &list->whiteKey : &list->blackKey);
  if(!*key) *key = ~(myRandom()*myRandom());
  p[i].promoGain = EasyProm(list->range); // flag easy promotion based on white view
  p[i].pieceKey = *key;
  p[i].promoFlag = 0;
  p[i].bulk = list->bulk;
  p[i].mobWeight = v > 600 ? 0 : v >= 400 ? 1 : v >= 300 ? 2 : v > 150 ? 3 : v >= 100 ? 2 : 0;
  if(Lance(list->range))
    p[i].mobWeight = 0, p[i].pst = list->range[4] ? PST_NEUTRAL : PST_LANCE; // keep back
  for(j=stm+2; j<= last[stm]; j+=2) {
    if(p[j].promo >= i) p[j].promo += 2;
  }
  if(royal[stm] >= i) royal[stm] += 2;
  if(kylin[stm] >= i) kylin[stm] += 2;
  if(p[i].value == (currentVariant == V_SHO ? 410 : 280) ) royal[stm] = i, p[i].pst = 0;
  p[i].qval = (currentVariant == V_TENJIKU ? list->ranking : 0); // jump-capture hierarchy
  return i;
}

void
SetUp(char *array, int var)
{
  int i, j, n, m, nr, color;
  char c, *q, name[3], prince = 0;
  PieceDesc *p1, *p2;
  last[WHITE] = 1; last[BLACK] = 0;
  royal[WHITE] = royal[BLACK] = 0;
  for(i=0; ; i++) {
//printf("next rank: %s\n", array);
    for(j = BW*i; ; j++) {
      int pflag=0;
      if(*array == '+') pflag++, array++;
      c = name[0] = *array++;
      if(!c) goto eos;
      if(c == '.') continue;
      if(c == '/') break;
      name[1] = name[2] = 0;
      if(c == ':') name[0] = *array++, name[1] = *array++;
      if(name[0] >= 'a') {
	color = BLACK;
	name[0] += 'A' - 'a';
	if(name[1]) name[1] += 'A' - 'a';
      } else color = WHITE;
      if(!strcmp(name, "CP") || pflag && !strcmp(name, "DE")) prince |= color+1; // remember if we added Crown Prince
      p1 = LookUp(name, var);
      if(!p1) printf("tellusererror Unknown piece '%s' in setup\n", name), exit(-1);
      if(pflag && p1->promoted) p1 = LookUp(p1->promoted, var); // use promoted piece instead
      n = AddPiece(color, p1);
      p[n].pos = j;
      if(p1->promoted[0] && !pflag) {
        if(!strcmp(p1->promoted, "CP")) prince |= color+1; // remember if we added Crown Prince
	p2 = LookUp(p1->promoted, var);
        m = AddPiece(color, p2);
	if(m <= n) n += 2;
	p[n].promo = m;
	p[n].promoFlag = IsUpwardCompatible(p2->range, p1->range) * DONT_DEFER + CAN_PROMOTE;
	if(Forward(p1->range)) p[n].promoFlag |= LAST_RANK; // Pieces that only move forward can't defer on last rank
	if(!strcmp(p1->name, "N")) p[n].promoFlag |= CANT_DEFER; // Knights can't defer on last 2 ranks
	p[n].promoFlag &= n&1 ? P_WHITE : P_BLACK;
	p[m].promo = -1;
	p[m].pos = ABSENT;
	if(p[m].value == LVAL) kylin[color] = n; // remember piece that promotes to Lion
      } else p[n].promo = -1; // unpromotable piece
//printf("piece = %c%-2s %d(%d) %d/%d\n", color ? 'w' : 'b', name, n, m, last[color], last[!color]);
    }
  }
 eos:
  // add dummy Kings if not yet added (needed to set royal[] to valid value!)
  if(!royal[WHITE]) p[AddPiece(WHITE, LookUp("K", V_CHU))].pos = ABSENT;
  if(!royal[BLACK]) p[AddPiece(BLACK, LookUp("K", V_CHU))].pos = ABSENT;
  // add dummy Crown Princes if not yet added
  if(!(prince & WHITE+1)) p[AddPiece(WHITE, LookUp("CP", V_CHU))].pos = ABSENT;
  if(!(prince & BLACK+1)) p[AddPiece(BLACK, LookUp("CP", V_CHU))].pos = ABSENT;
  for(i=0; i<8; i++)  fireFlags[i] = 0;
  for(i=2, n=1; i<10; i++) if(p[i].value == FVAL) {
    int x = p[i].pos; // mark all burn zones
    fireFlags[i-2] = n;
    if(x != ABSENT) for(j=0; j<8; j++) fireBoard[x+kStep[j]] |= n;
    n <<= 1;
  }
  for(i=0; i<BH; i++) for(j=0; j<BH; j++) board[BW*i+j] = EMPTY;
  for(i=WHITE+2; i<=last[WHITE]; i+=2) if(p[i].pos != ABSENT) {
    int g = p[i].promoGain;
    if(i == kylin[WHITE]) p[i].promoGain = 1.25*KYLIN, p[i].value += KYLIN;
//    if(j > 0 && p[i].pst == PST_STEPPER) p[i].pst = PST_WPPROM; // use white pre-prom bonus
    if(j > 0 && p[i].pst == PST_STEPPER && p[i].value >= 100)
	p[i].pst = p[i].value <= 150 ? PST_ADVANCE : PST_WPPROM;  // light steppers advance
    if(j > 0 && p[i].bulk == 6) p[i].pst = PST_WFLYER, p[i].mobWeight = 4; // SM defends zone
    if((j = p[i].promo) > 0 && g)
      p[i].promoGain = (p[j].value - p[i].value - g)*0.9, p[i].value = p[j].value - g;
    else p[i].promoGain = 0;
    board[p[i].pos] = i;
    rootEval += p[i].value + PST[p[i].pst + p[i].pos];
    promoDelta += p[i].promoGain;
    filling += p[i].bulk;
  } else p[i].promoGain = 0;
  for(i=BLACK+2; i<=last[BLACK]; i+=2) if(p[i].pos != ABSENT) {
    int g = p[i].promoGain;
//    if(j > 0 && p[i].pst == PST_STEPPER) p[i].pst = PST_BPPROM; // use black pre-prom bonus
    if(j > 0 && p[i].pst == PST_STEPPER && p[i].value >= 100)
	p[i].pst = p[i].value <= 150 ? PST_RETRACT : PST_BPPROM;  // light steppers advance
    if(j > 0 && p[i].pst == PST_WJUMPER) p[i].pst = PST_BJUMPER;  // use black pre-prom bonus
    if(j > 0 && p[i].bulk == 6) p[i].pst = PST_BFLYER, p[i].mobWeight = 4; // SM defends zone
    if((j = p[i].promo) > 0 && g)
      p[i].promoGain = (p[j].value - p[i].value - g)*0.9, p[i].value = p[j].value - g;
    else p[i].promoGain = 0;
    if(i == kylin[BLACK]) p[i].promoGain = 1.25*KYLIN, p[i].value += KYLIN;
    board[p[i].pos] = i;
    rootEval -= p[i].value + PST[p[i].pst + p[i].pos];
    promoDelta -= p[i].promoGain;
    filling += p[i].bulk;
  } else p[i].promoGain = 0;
  StackMultis(WHITE);
  StackMultis(BLACK);
  stm = WHITE; xstm = BLACK;
}

int myRandom()
{
  return rand() ^ rand()>>10 ^ rand() << 10 ^ rand() << 20;
}

void
Init (int var)
{
  int i, j, k;
  PieceDesc *pawn;

  if(var != SAME) { // the following should be already set if we stay in same variant (for TakeBack)
  currentVariant = variants[var].varNr;
  bWidth  = variants[var].boardWidth;
  bHeight = variants[var].boardRanks;
  zone    = variants[var].zoneDepth;
  array   = variants[var].array;
  }
  bsize = bWidth*bHeight;
  chuFlag = (currentVariant == V_CHU || currentVariant == V_LION);
  tenFlag = (currentVariant == V_TENJIKU);
  chessFlag = (currentVariant == V_CHESS || currentVariant == V_LION);
  stalemate = (currentVariant == V_CHESS || currentVariant == V_MAKRUK || currentVariant == V_LION);
  repDraws  = (stalemate || currentVariant == V_SHATRANJ);
  ll = 0; lr = bHeight - 1; ul = (bHeight - 1)*bWidth; ur = ul + bHeight - 1;
  pawn = LookUp("P", currentVariant); pVal = pawn ? pawn->value : 0; // get Pawn value

  for(i= -1; i<9; i++) { // board steps in linear coordinates
    kStep[i] = STEP(direction[i&7].x,   direction[i&7].y);       // King
    nStep[i] = STEP(direction[(i&7)+8].x, direction[(i&7)+8].y); // Knight
  }
  for(i=0; i<8; i++) neighbors[i+1] = kStep[i];

  for(i=0; i<8; i++) { // Lion double-move decoding tables
    for(j=0; j<8; j++) {
      epList[8*i+j] = kStep[i];
      toList[8*i+j] = kStep[j] + kStep[i];
      for(k=0; k<8*i+j; k++)
	if(epList[k] == toList[8*i+j] && toList[k] == epList[8*i+j])
	  reverse[k] = 8*i+j, reverse[8*i+j] = k;
    }
    // Lion-Dog triple moves
    toList[64+i] = 3*kStep[i]; epList[64+i] =   kStep[i];
    toList[72+i] = 3*kStep[i]; epList[72+i] = 2*kStep[i];
    toList[80+i] = 3*kStep[i]; epList[80+i] =   kStep[i]; ep2List[80+i] = 2*kStep[i];
    toList[88+i] =   kStep[i]; epList[88+i] = 2*kStep[i];
  }

  toList[100]   = BH - 2; epList[100]   = BH - 1; ep2List[100]   = BH - 3;
  toList[100+1] =      2; epList[100+1] =      0; ep2List[100+1] =      3;
  toList[100+2] = bsize - BH - 2; epList[100+2] = bsize - BH - 1; ep2List[100+2] = bsize - BH - 3;
  toList[100+3] = bsize - BW + 2; epList[100+3] = bsize - BW;     ep2List[100+3] = bsize - BW + 3;

  // fill distance table
  for(i=0; i<2*BSIZE; i++) {
    distance[i] = 0;
  }
//  for(i=0; i<8; i++)
//    for(j=1; j<BH; j++)
//      dist[j * kStep[i]] = j;
//  if(currentVariant == V_TENJIKU)
    for(i=1-BH; i<BH; i++) for(j=1-BH; j<BH; j++) dist[BW*i+j] = abs(i) > abs(j) ? abs(i) : abs(j);

  // hash key tables
  for(i=0; i<bsize; i++) squareKey[i] = ~(myRandom()*myRandom());

  // board edge
  for(i=0; i<BSIZE + 11*BHMAX + 6; i++) rawBoard[i] = EDGE;

  // promotion zones
  for(i=0; i<BH; i++) for(j=0; j<BH; j++) {
    char v = 0;
    if(i == 0)       v |= LAST_RANK & P_BLACK;
    if(i < 2)        v |= CANT_DEFER & P_BLACK;
    if(i < ZONE)     v |= (CAN_PROMOTE | DONT_DEFER) & P_BLACK; else v &= ~P_BLACK;
    if(i >= BH-ZONE) v |= (CAN_PROMOTE | DONT_DEFER) & P_WHITE; else v &= ~P_WHITE;
    if(i >= BH-2)    v |= CANT_DEFER & P_WHITE;
    if(i == BH-1)    v |= LAST_RANK & P_WHITE;
    promoBoard[BW*i + j] = v;
  }

  // piece-square tables
  for(j=0; j<BH; j++) {
   for(i=0; i<BH; i++) {
    int s = BW*i + j, d = BH*(BH-2) - abs(2*i - BH + 1)*(BH-1) - (2*j - BH + 1)*(2*j - BH + 1);
    PST[s] = 2*(i==0 | i==BH-1) + (i==1 | i==BH-2);         // last-rank markers in null table
    PST[PST_STEPPER+s] = d/4 - (i < 2 || i > BH-3 ? 3 : 0) - (j == 0 || j == BH-1 ? 5 : 0)
                    + 3*(i==zone || i==BH-zone-1);          // stepper centralization
    PST[PST_WJUMPER+s] = d/6;                               // double-stepper centralization
    PST[PST_SLIDER +s] = d/12 - 15*(i==BH/2 || i==(BH-1)/2);// slider centralization
    PST[PST_TRAP  +s] = j < 3 || j > BH-4 ? (i < 3 ? 7 : i == 3 ? 4 : i == 4 ? 2 : 0) : 0;
    PST[PST_CENTER+s] = ((BH-1)*(BH-1) - (2*i - BH + 1)*(2*i - BH + 1) - (2*j - BH + 1)*(2*j - BH + 1))/6;
    PST[PST_WPPROM+s] = PST[PST_BPPROM+s] = PST[PST_STEPPER+s]; // as stepper, but with pre-promotion bonus W/B
    PST[PST_BJUMPER+s] = PST[PST_WJUMPER+s];                // as jumper, but with pre-promotion bonus B
    PST[PST_ZONDIST+s] = BW*(zone - 1 - i);                 // board step to enter promo zone black
    PST[PST_ADVANCE+s] = PST[PST_WFLYER-s-1] = 2*(5*i+i*i) - (i >= zone)*6*(i-zone+1)*(i-zone+1)
	- (2*j - BH + 1)*(2*j - BH + 1)/BH + BH/2
	- 50 - 35*(j==0 || j == BH-1) - 15*(j == 1 || BH-2); // advance-encouraging table
    PST[PST_WFLYER +s] = PST[PST_LANCE-s-1] = (i == zone-1)*40 + (i == zone-2)*20 - 20;
    PST[PST_LANCE  +s] = (PST[PST_STEPPER+j] - PST[PST_STEPPER+s])/2; 
   }
   if(zone > 0) PST[PST_WPPROM+BW*(BH-1-zone) + j] += 10, PST[PST_BPPROM + BW*zone + j] += 10;
   if(j > (BH-1)/2 - 3 && j < BH/2 + 3)
	PST[PST_WPPROM + j] += 4, PST[PST_BPPROM + BW*(BH-1) + j] += 4; // fortress
   if(j > (BH-1)/2 - 2 && j < BH/2 + 2)
	PST[PST_WPPROM + BW + j] += 2, PST[PST_BPPROM + BW*(BH-2) + j] += 2; // fortress
#if KYLIN
   // pre-promotion bonuses for jumpers
   if(zone > 0) PST[PST_WJUMPER + BW*(BH-2-zone) + j] = PST[PST_BJUMPER + BW*(zone+1) + j] = 100,
                PST[PST_WJUMPER + BW*(BH-1-zone) + j] = PST[PST_BJUMPER + BW*zone + j] = 200;
#endif
  }

  p[EDGE].qval = 5; // tenjiku jump-capturer sentinel
}

int
PSTest ()
{
  int r, f, score, tot=0;
  for(r=0; r<BH; r++) for(f=0; f<BH; f++) {
    int s = BW*r+f;
    int piece = board[s];
    if(!piece) continue;
    score = p[piece].value + PST[p[piece].pst + s];
    if(piece & 1) tot += score; else tot -= score;
  }
  return tot;
}

int
Dtest ()
{
  int r, f, score, tot=0;
  for(r=0; r<BH; r++) for(f=0; f<BH; f++) {
    int s = BW*r+f;
    int piece = board[s];
    if(!piece) continue;
    score = p[piece].promoGain;
    if(piece & 1) tot += score; else tot -= score;
  }
  return tot;
}

int flag;

inline int
NewNonCapture (int x, int y, int promoFlags)
{
  if(board[y] != EMPTY) return 1; // edge, capture or own piece
//if(flag) printf("# add %c%d%c%d, pf=%d\n", x%BW+'a',x/BW,y%BW+'a',y/BW, promoFlags);
  if( (entryProm ? promoBoard[y] & ~promoBoard[x] & CAN_PROMOTE
                 : promoBoard[y] |  promoBoard[x]       ) & promoFlags ){ // piece can promote with this move
    moveStack[msp++] = moveStack[nonCapts];           // create space for promotion
    moveStack[nonCapts++] = x<<SQLEN | y | PROMOTE;   // push promotion
    if((promoFlags & promoBoard[y] & (CANT_DEFER | DONT_DEFER | LAST_RANK)) == 0) { // deferral could be a better alternative
      moveStack[msp++] = x<<SQLEN | y;                // push deferral
      if( (promoBoard[x] & CAN_PROMOTE) == 0 ) {      // enters zone
	moveStack[msp-1] |= DEFER;                    // flag that promo-suppression takes place after this move
      }
    }
  } else
    moveStack[msp++] = x<<SQLEN | y; // push normal move
//if(flag) printf("msp=%d nc=%d\n", msp, nonCapts);	
  return 0;
}

inline int
NewCapture (int x, int y, int promoFlags)
{
  if( (promoBoard[x] | promoBoard[y]) & promoFlags) { // piece can promote with this move
    moveStack[msp++] = x<<SQLEN | y | PROMOTE;        // push promotion
    if((promoFlags & promoBoard[y] & (CANT_DEFER | DONT_DEFER | LAST_RANK)) == 0) { // deferral could be a better alternative
      moveStack[msp++] = x<<SQLEN | y;                // push deferral
      if( (promoBoard[x] & CAN_PROMOTE) == 0 ) {      // enters zone
	moveStack[msp-1] |= DEFER;                    // flag that promo-suppression takes place after this move
      }
    }
  } else
    moveStack[msp++] = x<<SQLEN | y; // push normal move
  return 0;
}

char map[49]; // 7x7 map for area movers
char mapStep[] = { 7, 8, 1, -6, -7, -8, -1, 6 };
char rowMask[] = { 0100, 0140, 0160, 070, 034, 016, 07, 03, 01 };
char rows[9];

int
AreaStep (int from, int x, int flags, int n, int d)
{
  int i;
  for(i=0; i<8; i++) {
    int to = x + kStep[i], m = n + mapStep[i];
    if(board[to] == EDGE) continue; // off board
    if(map[m] >= d) continue;   // already done
    if(!map[m]) moveStack[msp++] = from<<SQLEN | to;
    map[m] = d;
    if(d > 1 && board[to] == EMPTY) AreaStep(from, to, flags, m, d-1);
  }
}

int
AreaMoves (int from, int piece, int range)
{
  int i;
  for(i=0; i<49; i++) map[i] = 0;
  map[3*7+7] = range;
  AreaStep(from, from, p[piece].promoFlag, 3*7+3, range);
}

void
MarkBurns (int x)
{ // make bitmap of squares in FI (7x7) neighborhood where opponents can be captured or burned
  int r=x>>5, f=x&15, top=8, bottom=0, b=0, t=8, left=0, right=8; // 9x9 area; assumes 32x16 board
  if(r < 4) bottom = 4 - r, rows[b=bottom-1] = 0; else
  if(r > 11) top   = 19 - r, rows[t=top+1] = 0; // adjust area to board edges
  if(f < 4) left   = 4 - f; else if(f > 11) right = 19 - f;
  for(r=bottom; r<=top; r++) {
    int mask = 0, y = x + 16*r;
    for(f=left; f <= right; f++) {
      if(board[y + f - (4*16+4)] != EMPTY && (board[y + f - (4*16+4)] & TYPE) == xstm)
	mask |= rowMask[f]; // on-rank attacks
    }
    rows[r+2] = mask;
  }
  for(r=b; r<=t-2; r++) rows[r] |= rows[r+1] | rows[r+2]; // smear vertically
}

void
GenCastlings ()
{ // castings for Lion Chess. Assumes board width = 8 and Kings on e-file, and K/R value = 280/300!
    int f = BH>>1, t = CASTLE;
    if(stm != WHITE) f += bsize - BW, t += 2;
    if(p[board[f]].value = 280) {
      if(p[board[f-4]].value == 300 && board[f-3] == EMPTY && board[f-2] == EMPTY && board[f-1] == EMPTY) moveStack[msp++] = f<<SQLEN | t+1;
      if(p[board[f+3]].value == 300 && board[f+1] == EMPTY && board[f+2] == EMPTY) moveStack[msp++] = f<<SQLEN | t;
    }
}

int
GenNonCapts (int promoSuppress)
{
  int i, j, nullMove = ABSENT;
  for(i=stm+2; i<=last[stm]; i+=2) {
    int x = p[i].pos, pFlag = p[i].promoFlag;
    if(x == ABSENT) continue;
    if(x == promoSuppress && chuFlag) pFlag = 0;
    for(j=0; j<8; j++) {
      int y, v = kStep[j], r = p[i].range[j];
      if(r < 0) { // jumping piece, special treatment
	if(r == N) { // pure Knightm do off-ray jump
	  NewNonCapture(x, x + nStep[j], pFlag);
	} else
	if(r >= S) { // in any case, do a jump of 2
	  int occup = NewNonCapture(x, x + 2*v, pFlag);
	  if(r < J) { // Lion power, also single step
	    if(!NewNonCapture(x, x + v, pFlag)) nullMove = x; else occup = 1;
	    if(r <= L) { // true Lion, also Knight jump
	      if(!occup & r < L) for(y=x+2*v; !NewNonCapture(x, y+=v, pFlag) && r == S; ); // BS and FF moves
	      v = nStep[j];
	      NewNonCapture(x, x + v, pFlag);
	    }
	  }
	} else
	if(r == M) { // FIDE Pawn; check double-move
	  if(!NewNonCapture(x, x+v, pFlag) && chessFlag && promoBoard[x-v] & LAST_RANK)
	    NewNonCapture(x, x+2*v, pFlag), moveStack[msp-1] |= DEFER; // use promoSuppress flag as e.p. flag
	}
	continue;
      }
      y = x;
      while(r-- > 0)
	if(NewNonCapture(x, y+=v, pFlag)) break;
    }
  }
  return nullMove;
}

void
report (int x, int y, int i)
{
}

int
MapOneColor (int start, int last, int *map)
{
  int i, j, k, totMob = 0;
  for(i=start+2; i<=last; i+=2) {
    int mob = 0;
    if(p[i].pos == ABSENT) continue;
    for(j=0; j<8; j++) {
      int x = p[i].pos, v = kStep[j], r = p[i].range[j];
      if(r < 0) { // jumping piece, special treatment
	if(r == N) {
	  x += nStep[j];
	  if(board[x] != EMPTY && board[x] != EDGE)
	    map[2*x + start] += one[8];
	} else
	if(r >= S) { // in any case, do a jump of 2
	  if(board[x + 2*v] != EMPTY && board[x + 2*v] != EDGE)
	    map[2*(x + 2*v) + start] += one[j], mob += (board[x + 2*v] ^ start) & 1;
	  if(r < J) { // Lion power, also single step
	    if(board[x + v] != EMPTY && board[x + v] != EDGE)
	      map[2*(x + v) + start] += one[j];
	    if(r == T) { // Lion Dog, also jump of 3
	      if(board[x + 3*v] != EMPTY && board[x + 3*v] != EDGE)
		map[2*(x + 3*v) + start] += one[j];
	    } else
	    if(r <= L) {  // true Lion, also Knight jump
	      if(r < L) { // Lion plus (limited) range
		int y = x, n = 0;
		r = (r == S ? 36 : 3);
		while(n++ <= r) {
		  if(board[y+=v] == EDGE) break;
		  if(board[y] != EMPTY) {
		    if(n > 2) map[2*y + start] += one[j]; // outside Lion range
		    break;
		  }
		}
	      }
	      v = nStep[j];
	      if(board[x + v] != EMPTY && board[x + v] != EDGE)
		map[2*(x + v) + start] += one[8];
	    }
	  }
	} else
	if(r == C) { // FIDE Pawn diagonal
	  if(board[x + v] != EMPTY && board[x + v] != EDGE)
	    map[2*(x + v) + start] += one[j];
	}
	continue;
      }
      while(r-- > 0) {
        if(board[x+=v] != EMPTY) {
	  mob += dist[x-v-p[i].pos];
	  if(board[x] != EDGE) map[2*x + start] += one[j], mob += (board[x] ^ start) & 1;
#if 1
	  if(p[i].range[j] > X) { // jump capturer
	    int c = p[i].qval;
	    if(p[board[x]].qval < c) {
	      x += v; // go behind directly captured piece, if jumpable
	      while(p[board[x]].qval < c) { // kludge alert: EDGE has qval = 5, blocking everything
		if(board[x] != EMPTY) {
//		  int n = map[2*x + start] & attackMask[j];
//		  map[2*x + start] += (n < 3*one[j] ? 3*one[j] : one[j]); // first jumper gets 2 extra (to ease incremental update)
		  map[2*x + start] += one[j]; // for now use true count
		}
		x += v;
	      }
	    }
	  }
#endif
	  break;
	}
      }
    }
    totMob += mob * p[i].mobWeight;
  }
if(!level) printf("# mobility %d = %d\n", start, totMob);
  return totMob;
}

void
MapFromScratch (int *map)
{
  int i;
  for(i=0; i<2*bsize; i++) map[i] = 0;
  mobilityScore  = MapOneColor(1, last[WHITE], map);
  mobilityScore -= MapOneColor(0, last[BLACK], map);
}

int
MakeMove(Move m, UndoInfo *u)
{
  int deferred = ABSENT;
  // first execute move on board
  u->from = m>>SQLEN & SQUARE;
  u->to = m & SQUARE;
  u->piece = board[u->from];
  board[u->from] = EMPTY;
  u->booty = 0;
  u->gain  = 0;
  u->loss  = 0;
  u->revMoveCount = cnt50++;
  u->savKeyL = hashKeyL;
  u->savKeyH = hashKeyH;
  u->epVictim[0] = EMPTY;
  u->saveDelta = promoDelta;
  u->filling = filling;

  if(p[u->piece].promoFlag & LAST_RANK) cnt50 = 0; // forward piece: move is irreversible
  // TODO: put in some test for forward moves of non-backward pieces?
//		int n = board[promoSuppress-1];
//		if( n != EMPTY && (n&TYPE) == xstm && p[n].value == 8 ) NewNonCapt(promoSuppress-1, 16, 0);

  if(p[u->piece].value == FVAL) { // move with Fire Demon
    int i, f=~fireFlags[u->piece-2];
    for(i=0; i<8; i++) fireBoard[u->from + kStep[i]] &= f; // clear old burn zone
  }

  if(m & (PROMOTE | DEFER)) {
    if(m & DEFER) { // essential deferral: inform caller, but nothing special
      deferred = u->to;
      u->new = u->piece;
    } else {
      p[u->piece].pos = ABSENT;
      u->new = p[u->piece].promo;
      u->booty = p[u->new].value - p[u->piece].value;
      cnt50 = 0; // promotion irreversible
    }
  } else u->new = u->piece;

  if(u->to >= SPECIAL) { // two-step Lion move
   if(u->to >= CASTLE) { // move Rook, faking it was an e.p. victim so that UnMake works automatically
    u->epSquare  = epList[u->to - SPECIAL];
    u->ep2Square = ep2List[u->to - SPECIAL];
    u->epVictim[0] = board[u->epSquare];  // kludgy: fake that King e.p. captured the Rook!
    u->epVictim[1] = board[u->ep2Square]; // should be EMPTY (but you never know, so save as well).
    board[u->ep2Square] = u->epVictim[0]; // but put Rook back
    board[u->epSquare]  = EMPTY;
    p[u->epVictim[0]].pos = u->ep2Square;
    p[u->epVictim[1]].pos = ABSENT;
    u->to       = toList[u->to - SPECIAL];
    hashKeyL ^= p[u->epVictim[0]].pieceKey * squareKey[u->epSquare];
    hashKeyH ^= p[u->epVictim[0]].pieceKey * squareKey[u->epSquare+BH];
    hashKeyL ^= p[u->epVictim[0]].pieceKey * squareKey[u->ep2Square];
    hashKeyH ^= p[u->epVictim[0]].pieceKey * squareKey[u->ep2Square+BH];
   } else {
    // take care of first e.p. victim
    u->epSquare = u->from + epList[u->to - SPECIAL]; // decode
    u->epVictim[0] = board[u->epSquare]; // remember for takeback
    board[u->epSquare] = EMPTY;       // and remove
    p[u->epVictim[0]].pos = ABSENT;
    // now take care of (optional) second e.p. victim
    u->ep2Square = u->from + ep2List[u->to - SPECIAL]; // This is the (already evacuated) from-square when there is none!
    u->epVictim[1] = board[u->ep2Square]; // remember
    board[u->ep2Square] = EMPTY;        // and remove
    p[u->epVictim[1]].pos = ABSENT;
    // decode the true to-square, and correct difEval and hash key for the e.p. captures
    u->to       = u->from + toList[u->to - SPECIAL];
    u->booty += p[u->epVictim[1]].value + PST[p[u->epVictim[1]].pst + u->ep2Square];
    u->booty += p[u->epVictim[0]].value + PST[p[u->epVictim[0]].pst + u->epSquare];
    u->gain  += p[u->epVictim[1]].value;
    u->gain  += p[u->epVictim[0]].value;
    promoDelta += p[u->epVictim[0]].promoGain;
    promoDelta += p[u->epVictim[1]].promoGain;
    filling  -= p[u->epVictim[0]].bulk;
    filling  -= p[u->epVictim[1]].bulk;
    hashKeyL ^= p[u->epVictim[0]].pieceKey * squareKey[u->epSquare];
    hashKeyH ^= p[u->epVictim[0]].pieceKey * squareKey[u->epSquare+BH];
    hashKeyL ^= p[u->epVictim[1]].pieceKey * squareKey[u->ep2Square];
    hashKeyH ^= p[u->epVictim[1]].pieceKey * squareKey[u->ep2Square+BH];
    if(p[u->piece].value != LVAL && p[u->epVictim[0]].value == LVAL) deferred |= PROMOTE; // flag non-Lion x Lion
    cnt50 = 0; // double capture irreversible
   }
  }

  if(u->fireMask & fireBoard[u->to]) { // we moved next to enemy Fire Demon (must be done after SPECIAL, to decode to-sqr)
    p[u->piece].pos = ABSENT;          // this is suicide: implement as promotion to EMPTY
    u->new = EMPTY;
    u->booty -= p[u->piece].value;
    cnt50 = 0;
  } else
  if(p[u->piece].value == FVAL) { // move with Fire Demon that survives: burn
    int i, f=fireFlags[u->piece-2];
    for(i=0; i<8; i++) {
	int x = u->to + kStep[i], burnVictim = board[x];
	fireBoard[x] |= f;  // mark new burn zone
	u->epVictim[i+1] = burnVictim; // remember all neighbors, just in case
	if(burnVictim != EMPTY && (burnVictim & TYPE) == xstm) { // opponent => actual burn
	  board[x] = EMPTY; // remove it
	  p[burnVictim].pos = ABSENT;
	  u->booty += p[burnVictim].value + PST[p[burnVictim].pst + x];
	  u->gain  += p[burnVictim].value;
	  promoDelta += p[burnVictim].promoGain;
	  filling  -= p[burnVictim].bulk;
	  hashKeyL ^= p[burnVictim].pieceKey * squareKey[x];
	  hashKeyH ^= p[burnVictim].pieceKey * squareKey[x + BH];
	  cnt50 = 0; // actually burning something makes the move irreversible
	}
    }
    u->epVictim[0] = EDGE; // kludge to flag to UnMake this is special move
  }

  u->booty += PST[p[u->new].pst + u->to] - PST[p[u->piece].pst + u->from];

  u->victim = board[u->to];
  p[u->victim].pos = ABSENT;
  filling += p[u->new].bulk - p[u->piece].bulk - p[u->victim].bulk;
  promoDelta += p[u->new].promoGain - p[u->piece].promoGain + p[u->victim].promoGain;
  u->booty += p[u->victim].value + PST[p[u->victim].pst + u->to];
  u->gain  += p[u->victim].value;
  if(u->victim != EMPTY) {
    cnt50 = 0; // capture irreversible
    if(attacks[2*u->to + xstm]) u->loss = p[u->piece].value; // protected
  }

  p[u->new].pos = u->to;
  board[u->to] = u->new;
  promoDelta = -promoDelta;

  hashKeyL ^= p[u->new].pieceKey * squareKey[u->to]
           ^  p[u->piece].pieceKey * squareKey[u->from]
           ^  p[u->victim].pieceKey * squareKey[u->to];
  hashKeyH ^= p[u->new].pieceKey * squareKey[u->to+BH]
           ^  p[u->piece].pieceKey * squareKey[u->from+BH]
           ^  p[u->victim].pieceKey * squareKey[u->to+BH];

  return deferred;
}

void
UnMake(UndoInfo *u)
{
  if(u->epVictim[0]) { // move with side effects
    if(u->epVictim[0] == EDGE) { // fire-demon burn
      int i, f=~fireFlags[u->piece-2];
      for(i=0; i<8; i++) {
	int x = u->to + kStep[i];
	fireBoard[x] &= f;
	board[x] = u->epVictim[i+1];
	p[board[x]].pos = x; // even EDGE should have dummy entry in piece list
      }
    } else { // put Lion victim of first leg back
      p[u->epVictim[1]].pos = u->ep2Square;
      board[u->ep2Square] = u->epVictim[1];
      p[u->epVictim[0]].pos = u->epSquare;
      board[u->epSquare] = u->epVictim[0];
    }
  }

  if(p[u->piece].value == FVAL) {
    int i, f=fireFlags[u->piece-2];
    for(i=0; i<8; i++) fireBoard[u->from + kStep[i]] |= f; // restore old burn zone
  }

  p[u->victim].pos = u->to;
  board[u->to] = u->victim;  // can be EMPTY

  p[u->new].pos = ABSENT; 
  p[u->piece].pos = u->from; // this can be the same as above
  board[u->from] = u->piece;

  cnt50 = u->revMoveCount;
  hashKeyL = u->savKeyL;
  hashKeyH = u->savKeyH;
  filling  = u->filling;
  promoDelta = u->saveDelta;
}
	
void
GenCapts(int sqr, int victimValue)
{ // generate all moves that capture the piece on the given square
  int i, range, att = attacks[2*sqr + stm];
//printf("GenCapts(%c%d,%d) %08x\n",sqr%BW+'a',sqr/BW,victimValue,att);
  if(!att) return; // no attackers at all!
  for(i=0; i<8; i++) {               // try all rays
    int x, v, jumper, jcapt=0;
    if(att & attackMask[i]) {        // attacked by move in this direction
      v = -kStep[i]; x = sqr;
      while( board[x+=v] == EMPTY ); // scan towards source until we encounter a 'stop'
//printf("stop @ %c%d (dir %d)\n",x%BW+'a',x/BW,i);
      if((board[x] & TYPE) == stm) {               // stop is ours
	int attacker = board[x], d = dist[x-sqr], r = p[attacker].range[i];
//printf("attacker %d, range %d, dist %d\n", attacker, r, d);
	if(r >= d || r < L && (d > 3 && r == S || d == 3 && r >= S)) { // it has a plain move in our direction that hits us
	  NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	  att -= one[i];
	  if(!(att & attackMask[i])) continue; // no more; next direction
	  jcapt = p[board[x]].qval;   // jump-capturer hierarchy
	  while(board[x+=v] == EMPTY);// one attack accounted for, but more to come, so skip to next stop
	}
      }
      // we get here when we are on a piece that dous not attack us through a (limited) ranging move,
      // it can be our own or an enemy with not (enough) range, or which is blocked
      do {
//printf("scan %x-%x (%d) dir=%d d=%d r=%d att=%o jcapt=%d qval=%d\n", sqr, x, board[x], i, dist[x-sqr], p[board[x]].range[i], att, jcapt, p[board[x]].qval);
      if((board[x] & TYPE) == stm) {   // stop is ours
	int attacker = board[x], d = dist[x-sqr], r = p[attacker].range[i];
	if(jcapt < p[attacker].qval) { // it is a range jumper that jumps over the barrier
	  if(p[attacker].range[i] > 1) { // assumes all jump-captures are infinite range
	    NewCapture(x, sqr, p[attacker].promoFlag);
	    att -= one[i];
	  }
//if(board[x] == EDGE) { printf("edge hit %x-%x dir=%d att=%o\n", sqr, x, i, att); continue; }
	} else
	if(r < 0) { // stop has non-standard moves
	  switch(p[attacker].range[i]) { // figure out what he can do (including multi-captures, which causes the complexity)
	    case F: // Lion power + 3-step (as in FF)
	    case S: // Lion power + ranging (as in BS)
	    case L: // Lion
	      if(d > 2) break;
	      NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	      att -= one[i];
	      // now the multi-captures of designated victim together with lower-valued piece
	      if(d == 2) { // primary victim on second ring; look for victims to take in passing
		if((board[sqr+v] & TYPE) == xstm && board[sqr+v] > board[sqr])
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
		if((i&1) == 0) { // orthogonal: two extra bent paths
		  v = kStep[i-1];
		  if((board[x+v] & TYPE) == xstm && board[x+v] > board[sqr])
		    NewCapture(x, SPECIAL + 8*(i-1&7) + (i+1&7) + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
		  v = kStep[i+1];
		  if((board[x+v] & TYPE) == xstm && board[x+v] > board[sqr])
		    NewCapture(x, SPECIAL + 8*(i+1&7) + (i-1&7) + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
		}
	      } else { // primary victim on first ring
		int j;
		for(j=0; j<8; j++) { // we can go on in 8 directions after we captured it in passing
		  int v = kStep[j];
		  if(sqr + v == x || board[sqr+v] == EMPTY) // hit & run; make sure we include igui (attacker is still at x!)
		    NewCapture(x, SPECIAL + 8*i + j + victimValue, p[attacker].promoFlag);
		  else if((board[sqr+v] & TYPE) == xstm && board[sqr+v] > board[sqr]) {    // double capture
		    NewCapture(x, SPECIAL + 8*i + j + victimValue, p[attacker].promoFlag); // other victim after primary
		    if(dist[sqr+v-x] == 1) // other victim also on first ring; reverse order is possible
		      NewCapture(x, SPECIAL + reverse[8*i + j] + victimValue, p[attacker].promoFlag);
		  }
		}
	      }
	      break;
	    case D: // linear Lion move (as in HF, SE)
	      if(d > 2) break;
	      NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	      att -= one[i];
	      if(d == 2) { // check if we can take intermediate with it
		if((board[x-v] & TYPE) == xstm && board[x-v] > board[sqr])
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p.
	      } else { // d=1; can move on to second, or move back for igui
		NewCapture(x, SPECIAL + 8*i + (i^4) + victimValue, p[attacker].promoFlag); // igui
		if(board[sqr-v] == EMPTY || (board[sqr-v] & TYPE) == xstm && board[sqr-v] > board[sqr])
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // hit and run
	      }
	      break;
	    case J: // plain jump (as in KY, PH)
	      if(d != 2) break;
	      NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	      att -= one[i];
	      break;
	    case C: // FIDE Pawn
	      if(d != 1) break;
	      NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	      att -= one[i];
	  }
	}
//printf("mask[%d] = %o\n", i, att);
	if((att & attackMask[i]) == 0) break;
      }
      // more attacks to come; san for next stop
      if(jcapt < p[board[x]].qval) jcapt = p[board[x]].qval; // raise barrier for range jumpers further upstream
      while(board[x+=v] == EMPTY); // this should never run off-board, if the attack map is not corrupted
      } while(1);
    }
  }
  // off-ray attacks
  if(att & 0700000000) { // Knight attack
    for(i=0; i<8; i++) {    // scan all knight jumps to locate source
      int x = sqr - nStep[i], attacker = board[x];
      if(attacker == EMPTY || (attacker & TYPE) != stm) continue;
      if(p[attacker].range[i] <= L && p[attacker].range[i] >= S || p[attacker].range[i] == N) { // has Knight jump in our direction
	NewCapture(x, sqr + victimValue, p[attacker].promoFlag);   // plain jump (as in N)
	if(p[attacker].range[i] < N) { // Lion power; generate double captures over two possible intermediates
	  int v = kStep[i]; // leftish path
	  if((board[x+v] & TYPE) == xstm && board[x+v] > board[sqr])
	    NewCapture(x, SPECIAL + 8*i + (i+1&7) + victimValue, p[attacker].promoFlag);
	  v = kStep[i+1];  // rightish path
	  if((board[x+v] & TYPE) == xstm && board[x+v] > board[sqr])
	    NewCapture(x, SPECIAL + 8*(i+1&7) + i + victimValue, p[attacker].promoFlag);
	}
      }
    }
  }
}

int
Guard (int sqr)
{
  int piece = board[sqr], val;
  if(piece == EDGE) return 0;
  val = p[piece].value;
  if(val == 201) return 3; // Elephant
  if(val == 152) return 2; // Tiger
  if(val == 151) return 1; // Gold
  return 0;
}

int
Fortress (int forward, int king, int lion)
{ // penalty for lack of Lion-proof fortress
  int rank = PST[king], anchor, r, l, q, res = 0;
  if(rank != 2) return 25*(rank-2);
  anchor = king + forward*(rank-1);

  q = Guard(anchor); l = Guard(anchor-1); r = Guard(anchor+1);
  if(!q) return l > 1 || r > 1 ? 0 : -25;
  if(q == 1) res = 40*(l > 1 && r > 1);           // TGT, EGT or TGE get 40
  else { // T or E in front of King
    if(l > 1) res = 30 + (r > 1)*(20 + 5*(q==2)); // TT., ET. or TE. (30), TET (50), TTE (55)
    else if(r > 1) res = 30;                      // .TT, .ET or .TE (30)
  }
  q = 0;
  if(filling > 32) {
    if(r > 1 && Guard(king+2) == 1) q += 10;
    if(l > 1 && Guard(king-2) == 1) q += 10; 
    q += 5*(Guard(king+1) == 1);
    q += 5*(Guard(king-1) == 1);
    if(filling < 96) q = q*(filling - 32)>>6;
  }
  return res + q;

  if(Guard(anchor) == 3 || Guard(anchor+1) == 3 || Guard(anchor-1) == 3) return 0;
  if(rank == 2 && Guard(king+1) == 3 || Guard(king-1) == 3) return -50;
  if(Guard(r=anchor) == 2 || Guard(r=anchor+1) == 2 || Guard(r=anchor-1) == 2)
    return -100 + 50*(Guard(r + forward + 1) == 3 || Guard(r + forward - 1) == 3);
  return -300;

  for(r=anchor+1; Guard(r) > 1; r++);
  for(l=anchor-1; Guard(l) > 1; l--);
//if(PATH) printf("# l=%d r=%d\n", l, r);
  if(Guard(anchor) < 2) {
    if(r - anchor  > anchor - l || // largest group, or if equal, group that contains elephant
       r - anchor == anchor - l && Guard(r-1) == 3) l = anchor; else r = anchor;
  }
  switch(r - l) {
    case 1: q = 15; break;                // no shelter at all, maximum penalty
    case 2: if(Guard(l+1) == 3) q = 10;   // single Elephant offers some shelter
	    else if(Guard(l+forward) == 3 || Guard(l+forward+2) == 3) q = 8; // better if Tiger diagonally in front of it
	    else q = 14;                  // singe tiger almost no help;
	    break;
    case 3: q = 5 - (Guard(l+1) == 3 || Guard(l+3) == 3); break; // pair is better if it contains Elephant
    case 4: q = (Guard(l+2) != 3);        // 3 wide: perfect, or nearly so if Elephant not in middle
    default: ;
  }
//if(PATH) printf("# fortress %d: q=%d l=%d r=%d\n", anchor, q, l, r);
  return (dist[lion - king] - 23)*q;      // reduce by ~half if Lion very far away
}

int
Surround (int stm, int king, int start, int max)
{
  int i, s=0;
  for(i=start; i<9; i++) {
    int v, piece, sq = king + neighbors[i];
    if((piece = board[sq]) == EDGE || !piece || piece&1^stm) continue;
    if(p[piece].promoGain) continue;
    v = p[piece].value;
    s += -(v > 70) & v;
  }
  return (s > max ? max : s);
}

int
Ftest (int side)
{
  int lion = ABSENT, king;
  if(p[side+2].value == LVAL) lion = p[side+2].pos;
  if(lion == ABSENT && p[side+4].value == LVAL) lion = p[side+4].pos;
  king = p[royal[1-side]].pos; if(king == ABSENT) king = p[royal[1-side]+1].pos;
  return lion == ABSENT ? 0 : Fortress(side ? -BW : BW, king, lion);
}

int
Evaluate (int difEval)
{
  int wLion = ABSENT, bLion = ABSENT, wKing, bKing, score=mobilityScore, f, i, j, max=512;

  if(tsume) return difEval;

  if(p[WHITE+2].value == LVAL) wLion = p[WHITE+2].pos;
  if(p[BLACK+2].value == LVAL) bLion = p[BLACK+2].pos;
  if(wLion == ABSENT && p[WHITE+4].value == LVAL) wLion = p[WHITE+4].pos;
  if(bLion == ABSENT && p[BLACK+4].value == LVAL) bLion = p[BLACK+4].pos;

#ifdef LIONTRAP
# define lionTrap (PST + PST_TRAP)
  // penalty for Lion in enemy corner, when enemy Lion is nearby
  if(wLion != ABSENT && bLion != ABSENT) { // both have a Lion
      static int distFac[36] = { 0, 0, 10, 9, 8, 7, 5, 3, 1 };
      score -= ( (1+9*!attacks[2*wLion+WHITE]) * lionTrap[BW*(BH-1)+BH-1-wLion]
               - (1+9*!attacks[2*bLion+BLACK]) * lionTrap[bLion] ) * distFac[dist[wLion - bLion]];
  }

# ifdef WINGS
  // bonus if corner lances are protected by Lion-proof setup (FL + C/S)
  if(bLion != ABSENT) {
    if((p[board[BW+lr]].value == 320 || p[board[BW+lr]].value == 220) && 
        p[board[ll+1]].value == 150 && p[board[ll+BW+2]].value == 100) score += 20 + 20*!p[board[ll]].range[2];
    if((p[board[BW+lr]].value == 320 || p[board[BW+lr]].value == 220) &&
        p[board[lr-1]].value == 150 && p[board[lr+BW-2]].value == 100) score += 20 + 20*!p[board[lr]].range[2];
  }
  if(wLion != ABSENT) {
    if((p[board[ul-BW]].value == 320 || p[board[ul-BW]].value == 220) &&
        p[board[ul+1]].value == 150 && p[board[ul-BW+2]].value == 100) score -= 20 + 20*!p[board[ul]].range[2];
    if((p[board[ur-BW]].value == 320 || p[board[ur-BW]].value == 220) &&
        p[board[ur-1]].value == 150 && p[board[ur-BW-2]].value == 100) score -= 20 + 20*!p[board[ur]].range[2];
  }
# endif
#endif

#ifdef KINGSAFETY
  // basic centralization in end-game (also facilitates bare-King mating)
  wKing = p[royal[WHITE]].pos; if(wKing == ABSENT) wKing = p[royal[WHITE]+2].pos;
  bKing = p[royal[BLACK]].pos; if(bKing == ABSENT) bKing = p[royal[BLACK]+2].pos;
  if(filling < 32) {
    int lead = (stm == WHITE ? difEval : -difEval);
    score += (PST[PST_CENTER+wKing] - PST[PST_CENTER+bKing])*(32 - filling) >> 7;
    if(lead  > 100) score -= PST[PST_CENTER+bKing]*(32 - filling) >> 3; // white leads, drive black K to corner
    if(lead < -100) score += PST[PST_CENTER+wKing]*(32 - filling) >> 3; // black leads, drive white K to corner
    max = 16*filling;
  }

# ifdef FORTRESS
  f = 0;
  if(bLion != ABSENT) f += Fortress( BW, wKing, bLion);
  if(wLion != ABSENT) f -= Fortress(-BW, bKing, wLion);
  score += (filling < 192 ? f : f*(224 - filling) >> 5); // build up slowly
# endif

# ifdef KSHIELD
  score += Surround(WHITE, wKing, 1, max) - Surround(BLACK, bKing, 1, max) >> 3;
# endif

#endif

#if KYLIN
  // bonus for having Kylin in end-game, where it could promote to Lion
  // depends on board population, defenders around zone entry and proximity to zone
  if(filling < 128) {
    int sq;
    if((wLion = kylin[WHITE]) && (sq = p[wLion].pos) != ABSENT) {
      int anchor = sq - PST[5*BW*BH - 1 - sq]; // FIXME: PST_ZONDIST indexed backwards
      score += (512 - Surround(BLACK, anchor, 0, 512))*(128 - filling)*PST[p[wLion].pst + sq] >> 15;
    }
    if((bLion = kylin[BLACK]) && (sq = p[bLion].pos) != ABSENT) {
      int anchor = sq + PST[PST_ZONDIST + sq];
      score -= (512 - Surround(WHITE, anchor, 0, 512))*(128 - filling)*PST[p[bLion].pst + sq] >> 15;
    }
  }
#endif

#ifdef PAWNBLOCK
  // penalty for blocking own P or GB: 20 by slider, 10 by other, but 50 if only RETRACT mode is straight back
  for(i=last[WHITE]; i > 1 && p[i].value<=50; i-=2) {
    if((f = p[i].pos) != ABSENT) { // P present,
      if((j = board[f + BW])&1) // square before it white (odd) piece
	score -= 10 + 10*(p[j].promoGain > 0) + 30*!(p[j].range[3] || p[j].range[5] || p[j].value==50);
      if((j = board[f - BW])&1) // square behind it white (odd) piece
	score += 7*(p[j].promoGain == 0 & p[j].value<=151);
    }
  }
  for(i=last[BLACK]; i > 1 && p[i].value<=50; i-=2) {
    if((f = p[i].pos) != ABSENT) { // P present,
      if((j = board[f - BW]) && !(j&1)) // square before non-empty and even (black)
	score += 10 + 10*(p[j].promoGain > 0) + 30*!(p[j].range[1] || p[j].range[7] || p[j].value==50);
      if((j = board[f + BW]) && !(j&1)) // square behind non-empty and even (black)
	score -= 7*(p[j].promoGain == 0 & p[j].value<=151);
    }
  }
#endif

#ifdef TANDEM
    if(zone > 0) {
      int rw = BW*(BH-1-zone), rb = BW*zone, h=0;
      for(f=0; f<BH; f++) {
	if(p[board[rw+f]].pst == PST_ADVANCE) {
	  h += (p[board[rw+f-BW]].pst == PST_ADVANCE);
	  if(f > 0)    h += (p[board[rw+f-BW-1]].pst == PST_ADVANCE);
	  if(f+1 < BH) h += (p[board[rw+f-BW+1]].pst == PST_ADVANCE);
	}
	if(p[board[rb+f]].pst == PST_ADVANCE) {
	  h -= (p[board[rb+f+BW]].pst == PST_RETRACT);
	  if(f > 0)    h -= (p[board[rb+f+BW-1]].pst == PST_RETRACT);
	  if(f+1 < BH) h -= (p[board[rb+f+BW+1]].pst == PST_RETRACT);
	}
      }
      score += h*TANDEM;
    }
#endif

  return difEval - (filling*promoDelta >> 8) + (stm ? score : -score);
}

inline void
FireSet (UndoInfo *tb)
{ // set fireFlags acording to remaining presene of Fire Demons
  int i;
  for(i=stm+2; p[i].value == FVAL; i++) // Fire Demons are always leading pieces in list
    if(p[i].pos != ABSENT) tb->fireMask |= fireFlags[i-2];
}

void TerminationCheck();

#define QSdepth 4

int
Search (int alpha, int beta, int difEval, int depth, int lmr, int oldPromo, int promoSuppress, int threshold)
{
  int i, j, k, phase, king, nextVictim, to, defer, autoFail=0, inCheck = 0, late=100000, ep;
  int firstMove, oldMSP = msp, curMove, sorted, bad, dubious, bestMoveNr;
  int resDep, iterDep, ext;
  int myPV = pvPtr;
  int score, bestScore, oldBest, curEval, iterAlpha;
  Move move, nullMove;
  UndoInfo tb;
#ifdef HASH
  Move hashMove; int index, nr, hit;
#endif
if(PATH) /*pboard(board),pmap(attacks, BLACK),*/printf("search(%d) {%d,%d} eval=%d, stm=%d (flag=%d)\n",depth,alpha,beta,difEval,stm,abortFlag),fflush(stdout);
  xstm = stm ^ WHITE;
//printf("map made\n");fflush(stdout);

  // in-check test and TSUME filter
  {
    k = p[king=royal[stm]].pos;
    if( k == ABSENT) {
      if((k = p[king + 2].pos) == ABSENT && (!tsume || tsume & stm+1))
        return -INF;   // lose when no King (in tsume only for side to be mated)
    } else if(p[king + 2].pos != ABSENT) {
      if(tsume && tsume & stm+1) {
	retDep = 60; return INF; // we win when not in check
      }
      k = ABSENT; // two kings is no king...
    }
    if( k != ABSENT) { // check is possible
      if(!attacks[2*k + xstm]) {
	if(tsume && tsume & stm+1) {
	  retDep = 60; return INF; // we win when not in check
        }
      }
#ifdef CHECKEXT
      else { inCheck = 1; if(depth >= QSdepth) depth++; }
#endif
    }
  }

if(!level) {for(i=0; i<5; i++)printf("# %d %08x, %d\n", i, repStack[200-i], checkStack[200-i]);}
  // KING CAPTURE
  k = p[king=royal[xstm]].pos;
  if( k != ABSENT) {
    if(attacks[2*k + stm]) {
      if( p[king + 2].pos == ABSENT ) return INF; // we have an attack on his only King
    }
  } else { // he has no king! Test for attacks on Crown Prince
    k = p[king + 2].pos;
    if(k == ABSENT ? !tsume : attacks[2*k + stm]) return INF; // we have attack on Crown Prince
  }
//printf("King safe\n");fflush(stdout);
  // EVALUATION & WINDOW SHIFT
  curEval = Evaluate(difEval) -20*inCheck;
  alpha -= (alpha < curEval);
  beta  -= (beta <= curEval);

  if(!(nodes++ & 4095)) TerminationCheck();
  pv[pvPtr++] = 0; // start empty PV, directly behind PV of parent
  if(inCheck) lmr = 0; else depth -= lmr; // no LMR of checking moves

  firstMove = curMove = sorted = msp += 50; // leave 50 empty slots in front of move list
  iterDep = -(depth == 0); tb.fireMask = phase = 0;

#ifdef HASH
  index = hashKeyL ^ 327*stm ^ (oldPromo + 987981)*(63121 + promoSuppress);
  index = index + (index >> 16) & hashMask;
  nr = (hashKeyL >> 30) & 3; hit = -1;
  if(hashTable[index].lock[nr] == hashKeyH) hit = nr; else
  if(hashTable[index].lock[4]  == hashKeyH) hit = 4;
if(PATH) printf("# probe hash index=%x hit=%d\n", index, hit),fflush(stdout);
  if(hit >= 0) {
    bestScore = hashTable[index].score[hit];
    hashMove = hashTable[index].move[hit];
    if((bestScore <= alpha || hashTable[index].flag[hit] & H_LOWER) &&
       (bestScore >= beta  || hashTable[index].flag[hit] & H_UPPER)   ) {
      iterDep = resDep = hashTable[index].depth[hit]; bestMoveNr = 0;
      if(!level) iterDep = 0; // no hash cutoff in root
      if(lmr && bestScore <= alpha && iterDep == depth) depth ++, lmr--; // self-deepening LMR
      if(pvCuts && iterDep >= depth && hashMove && bestScore < beta && bestScore > alpha)
	iterDep = depth - 1; // prevent hash cut in PV node
    }
  } else { // decide on replacement
    if(depth >= hashTable[index].depth[nr] ||
       depth+1 == hashTable[index].depth[nr] && !(nodes&3)) hit = nr; else hit = 4;
    hashMove = 0;
  }
if(PATH) printf("# iterDep = %d score = %d move = %s\n",iterDep,bestScore,MoveToText(hashMove,0)),fflush(stdout);
#endif

  if(depth > QSdepth && iterDep < QSdepth) iterDep = QSdepth; // full-width: start at least from 1-ply
if(PATH)printf("iterDep=%d\n", iterDep);
  while(++iterDep <= depth) {
if(flag && depth>= 0) printf("iter %d:%d\n", depth,iterDep),fflush(stdout);
    oldBest = bestScore;
    iterAlpha = alpha; bestScore = -INF; bestMoveNr = 0; resDep = 60;
if(PATH)printf("new iter %d\n", iterDep);
    if(depth <= QSdepth) {
      bestScore = curEval; resDep = QSdepth;
      if(bestScore > alpha) {
	alpha = bestScore;
if(PATH)printf("stand pat %d\n", bestScore);
	if(bestScore >= beta) goto cutoff;
      }
    }
    for(curMove = firstMove; ; curMove++) { // loop over moves
if(flag && depth>= 0) printf("phase=%d: first/curr/last = %d / %d / %d\n", phase, firstMove, curMove, msp);fflush(stdout);
      // MOVE SOURCE
      if(curMove >= msp) { // we ran out of moves; generate some new
if(PATH)printf("new moves, phase=%d\n", phase);
	switch(phase) {
	  case 0: // null move
#ifdef NULLMOVE
	    if(depth > QSdepth && curEval >= beta && !inCheck && filling > 10) {
              int nullDep = depth - 3;
	      stm ^= WHITE;
	      score = -Search(-beta, 1-beta, -difEval, nullDep<QSdepth ? QSdepth : nullDep, 0, promoSuppress & SQUARE, ABSENT, INF);
	      xstm = stm; stm ^= WHITE;
	      if(score >= beta) { msp = oldMSP; retDep += 3; pvPtr = myPV; return score + (score < curEval); }
//	      else depth += lmr, lmr = 0;
	    }
#endif
	    if(tenFlag) FireSet(&tb); // in tenjiku we must identify opposing Fire Demons to perform any moves
//if(PATH) printf("mask=%x\n",tb.fireMask),pbytes(fireBoard);
	    phase = 1;
	  case 1: // hash move
	    phase = 2;
#ifdef HASH
	    if(hashMove && (depth > QSdepth || // must be capture in QS
		 (hashMove & SQUARE) >= SPECIAL || board[hashMove & SQUARE] != EMPTY)) {
	      moveStack[sorted = msp++] = hashMove;
	      goto extractMove;
	    }
#endif
	  case 2: // capture-gen init
	    nextVictim = xstm; autoFail = (depth == 0);
	    phase = 3;
	  case 3: // generate captures
if(PATH) printf("%d:%2d:%2d next victim %d/%d\n",level,depth,iterDep,curMove,msp);
	    while(nextVictim < last[xstm]) {          // more victims exist
	      int group, to = p[nextVictim += 2].pos; // take next
	      if(to == ABSENT) continue;              // ignore if absent
	      if(!attacks[2*to + stm]) continue;      // skip if not attacked
	      group = p[nextVictim].value;            // remember value of this found victim
	      if(iterDep <= QSdepth + 1 && 2*group + curEval + 30 < alpha) {
		resDep = QSdepth + 1; nextVictim -= 2;
		if(bestScore < 2*group + curEval + 30) bestScore = 2*group + curEval + 30;
		goto cutoff;
	      }
if(PATH) printf("%d:%2d:%2d group=%d, to=%c%d\n",level,depth,iterDep,group,to%BW+'a',to/BW+ONE);
	      GenCapts(to, 0);
if(PATH) printf("%d:%2d:%2d first=%d msp=%d\n",level,depth,iterDep,firstMove,msp);
	      while(nextVictim < last[xstm] && p[nextVictim+2].value == group) { // more victims of same value exist
		to = p[nextVictim += 2].pos;          // take next
		if(to == ABSENT) continue;            // ignore if absent
		if(!attacks[2*to + stm]) continue;    // skip if not attacked
if(PATH) printf("%d:%2d:%2d p=%d, to=%c%d\n",level,depth,iterDep,nextVictim,to%BW+'a',to/BW+ONE);
		GenCapts(to, 0);
if(PATH) printf("%d:%2d:%2d msp=%d\n",level,depth,iterDep,msp);
	      }
if(PATH) printf("captures on %d generated, msp=%d, group=%d, threshold=%d\n", nextVictim, msp, group, threshold);
	      goto extractMove; // in auto-fail phase, only search if they might auto-fail-hi
	    }
if(PATH) printf("# autofail=%d\n", autoFail);
	    if(autoFail) { // non-captures cannot auto-fail; flush queued captures first
if(PATH) printf("# autofail end (%d-%d)\n", firstMove, msp);
	      autoFail = 0; curMove = firstMove - 1; continue; // release stashed moves for search
	    }
	    phase = 4; // out of victims: all captures generated
	    if(chessFlag && (ep = promoSuppress & SQUARE) != ABSENT) { // e.p. rights. Create e.p. captures as Lion moves
		int n = board[ep-1], old = msp; // a-side neighbor of pushed pawn
		if( n != EMPTY && (n&TYPE) == stm && p[n].value == pVal ) NewCapture(ep-1, SPECIAL + 20 - 4*stm, 0);
		n = board[ep+1];      // h-side neighbor of pushed pawn
		if( n != EMPTY && (n&TYPE) == stm && p[n].value == pVal ) NewCapture(ep+1, SPECIAL + 52 - 4*stm, 0);
		if(msp != old) goto extractMove; // one or more e.p. capture were generated
	    }
	  case 4: // dubious captures
#if 0
	    while( dubious < framePtr + 250 ) // add dubious captures back to move stack
	      moveStack[msp++] = moveStack[dubious++];
	    if(curMove != msp) break;
#endif
	    phase = 5;
	  case 5: // killers
	    if(depth <= QSdepth) { if(resDep > QSdepth) resDep = QSdepth; goto cutoff; }
	    phase = 6;
	  case 6: // non-captures
	    nonCapts = msp;
	    nullMove = GenNonCapts(oldPromo);
	    if(msp == nonCapts) goto cutoff;
#ifdef KILLERS
	    { // swap killers to front
	      Move h = killer[level][0]; int j = curMove;
	      for(i=curMove; i<msp; i++) if(moveStack[i] == h) { moveStack[i] = moveStack[j]; moveStack[j++] = h; break; }
	      h = killer[level][1];
	      for(i=curMove; i<msp; i++) if(moveStack[i] == h) { moveStack[i] = moveStack[j]; moveStack[j++] = h; break; }
	      late = j;
	    }
#else
	    late = j;
#endif
	    phase = 7;
	    sorted = msp; // do not sort noncapts
	    break;
	  case 7: // bad captures
	  case 8: // PV null move
	    phase = 9;
if(PATH) printf("# null = %0x\n", nullMove);
	    if(nullMove != ABSENT) {
	      moveStack[msp++] = nullMove + (nullMove << SQLEN) | DEFER; // kludge: setting DEFER guarantees != 0, and has no effect
	      break;
	    }
//printf("# %d. sqr = %08x null = %08x\n", msp, nullMove, moveStack[msp-1]);
	  case 9:
	    goto cutoff;
	}
      }

      // MOVE EXTRACTION
    extractMove:
//if(flag & depth >= 0 || (PATH)) printf("%2d:%d extract %d/%d\n", depth, iterDep, curMove, msp);
      if(curMove > sorted) {
	move = moveStack[sorted=j=curMove];
	for(i=curMove+1; i<msp; i++)
	  if(moveStack[i] > move) move = moveStack[j=i]; // search move with highest priority
	moveStack[j] = moveStack[curMove]; moveStack[curMove] = move; // swap highest-priority move to front of remaining
	if(move == 0) { msp = curMove--; continue; } // all remaining moves must be 0; clip off move list
      } else {
	move = moveStack[curMove];
	if(move == 0) continue; // skip invalidated move
      }
if(flag & depth >= 0) printf("%2d:%d found %d/%d %08x %s\n", depth, iterDep, curMove, msp, moveStack[curMove], MoveToText(moveStack[curMove], 0));

      // RECURSION
      stm ^= WHITE;
      defer = MakeMove(move, &tb);
      ext = (depth == 0); // when out of depth we extend captures if there was no auto-fail-hi

//      if(level == 1 && randomize) tb.booty += (hashKeyH * seed >> 24 & 31) - 20;

      if(autoFail) {
	UnMake(&tb); // never search moves during auto-fail phase
	xstm = stm; stm ^= WHITE;
	if(tb.gain <= threshold) { // we got to moves that cannot possibly auto-fail
	  autoFail = 0; curMove = firstMove-1; continue; // release all for search
	}
        if(tb.gain - tb.loss > threshold) {
          bestScore = INF+1; resDep = 0; goto leave; // auto-fail-hi
        } else continue; // ignore for now if not obvious refutation
      }

if(flag & depth >= 0) printf("%2d:%d made %d/%d %s\n", depth, iterDep, curMove, msp, MoveToText(moveStack[curMove], 0));
      for(i=2; i<=cnt50; i+=2) if(repStack[level-i+200] == hashKeyH) {
	retDep = iterDep;
	if(repDraws) { score = 0; goto repetition; }
	if(!allowRep) {
	  moveStack[curMove] = 0;         // erase forbidden move
	  if(!level) repeatMove[repCnt++] = move & 0xFFFFFF; // remember outlawed move
	} else { // check for perpetuals
//	  int repKey = 1;
//	  for(i-=level; i>1; i-=2) {repKey &= checkStack[200-i]; if(!level)printf("# repkey[%d] = %d\n", 200-i, repKey);}
	  if(inCheck) { score = INF-20; goto repetition; } // we might be subject to perpetual check: score as win
	  if(i == 2 && repStack[level+199] == hashKeyH) { score = INF-20; goto repetition; } // consecutive passing
	}
	score = -INF + 8*allowRep; goto repetition;
      }
      repStack[level+200] = hashKeyH;

if(PATH) printf("%d:%2d:%d %3d %6x %-10s %6d %6d\n", level, depth, iterDep, curMove, moveStack[curMove], MoveToText(moveStack[curMove], 0), score, bestScore),fflush(stdout);
path[level++] = move;
attacks += 2*bsize;
MapFromScratch(attacks); // for as long as incremental update does not work.
//if(flag & depth >= 0) printf("%2d:%d mapped %d/%d %s\n", depth, iterDep, curMove, msp, MoveToText(moveStack[curMove], 0));
//if(PATH) pmap(attacks, stm);
      if(chuFlag && p[tb.victim].value == LVAL) {// verify legality of Lion capture in Chu Shogi
	score = 0;
	if(p[tb.piece].value == LVAL) {          // Ln x Ln: can make Ln 'vulnerable' (if distant and not through intemediate > GB)
	  if(dist[tb.from-tb.to] != 1 && attacks[2*tb.to + stm] && p[tb.epVictim[0]].value <= 50)
	    score = -INF;                           // our Lion is indeed made vulnerable and can be recaptured
	} else {                                    // other x Ln
	  if(promoSuppress & PROMOTE) {             // non-Lion captures Lion after opponent did same
	    if(!okazaki || attacks[2*tb.to + stm]) score = -INF;
	  }
	  defer |= PROMOTE;                         // if we started, flag  he cannot do it in reply
	}
        if(score == -INF) {
          if(level == 1) repeatMove[repCnt++] = move & 0xFFFFFF | (p[tb.piece].value == LVAL ? 3<<24 : 1 << 24);
          moveStack[curMove] = 0; // zap illegal moves
          goto abortMove;
        }
      }
#if 1
      score = -Search(-beta, -iterAlpha, -difEval - tb.booty, iterDep-1+ext,
                       curMove >= late && iterDep > QSdepth + LMR,
                                                      promoSuppress & ~PROMOTE, defer, depth ? INF : tb.gain);

#else
      score = 0;
#endif
    abortMove:
attacks -= 2*bsize;
level--;
    repetition:
      UnMake(&tb);
      xstm = stm; stm ^= WHITE;
      if(abortFlag > 0) { // unwind search
printf("# abort (%d) @ %d\n", abortFlag, level);
        if(curMove == firstMove) bestScore = oldBest, bestMoveNr = firstMove; // none searched yet
        goto leave;
      }
#if 1
if(PATH) printf("%d:%2d:%d %3d %6x %-10s %6d %6d  (%d)\n", level, depth, iterDep, curMove, moveStack[curMove], MoveToText(moveStack[curMove], 0), score, bestScore, GetTickCount()),fflush(stdout);

      // ALPHA-BETA STUFF
      if(score > bestScore) {
	bestScore = score; bestMoveNr = curMove;
	if(score > iterAlpha) {
	  iterAlpha = score;
	  if(curMove < firstMove + 5) { // if not too much work, sort move to front
	    int i;
	    for(i=curMove; i>firstMove; i--) {
	      moveStack[i] = moveStack[i-1];
	    }
	    moveStack[firstMove] = move;
	  } else { // late move appended in front of list, and leaves hole in list
	    moveStack[--firstMove] = move;
	    moveStack[curMove] = 0;
	  }
	  bestMoveNr = firstMove;
	  if(score >= beta) { // beta cutoff
#ifdef KILLERS
	    if(iterDep == depth && move != killer[level][0]
		 && (tb.victim == EMPTY && (move & SQUARE) < SPECIAL)) {
	      // update killer
	      killer[level][1] = killer[level][0]; killer[level][0] = move;
	    }
#endif
	    resDep = retDep+1-ext;
	    goto cutoff;
	  }
	  { int i=pvPtr;
	    for(pvPtr = myPV+1; pv[pvPtr++] = pv[i++]; ); // copy daughter PV
	    pv[myPV] = move;                              // behind our move (pvPtr left at end of copy)
	  }
	}

      }
      if(retDep+1-ext < resDep) resDep = retDep+1-ext;
#endif
    } // next move
  cutoff:
    if(!level) { // root node
      lastRootIter = GetTickCount() - startTime;
      if(postThinking > 0) {
        int i;   // WB thinking output
	printf("%d %d %d %d", iterDep-QSdepth, bestScore, lastRootIter/10, nodes);
        if(ponderMove) printf(" (%s)", MoveToText(ponderMove, 0));
	for(i=0; pv[i]; i++) printf(" %s", MoveToText(pv[i], 0));
        if(iterDep == QSdepth+1) printf(" { root eval = %4.2f dif = %4.2f; abs = %4.2f f=%d D=%4.2f %d/%d}", curEval/100., difEval/100., PSTest()/100., filling, promoDelta/100., Ftest(0), Ftest(1));
	printf("\n");
        fflush(stdout);
      }
      if(!(abortFlag & 1) && GetTickCount() - startTime > tlim1) break; // do not start iteration we can (most likely) not finish
    }
    if(resDep > iterDep) iterDep = resDep; // skip iterations if we got them for free
#ifdef LMR
    if(lmr && bestScore <= alpha && iterDep == depth)
      depth++, lmr--; // self-deepen on fail-low reply to late move by lowering reduction
#endif
    if(stalemate && bestScore == -INF && !inCheck) bestScore = 0; // stalemate
#ifdef HASH
    // hash store
    hashTable[index].lock[hit]  = hashKeyH;
    hashTable[index].depth[hit] = resDep;
    hashTable[index].score[hit] = bestScore;
    hashTable[index].flag[hit]  = (bestScore < beta) * H_UPPER;
    if(bestScore > alpha) {
      hashTable[index].flag[hit] |= H_LOWER;
      hashTable[index].move[hit]  = bestMoveNr ? moveStack[bestMoveNr] : 0;
    } else hashTable[index].move[hit] = 0;
#endif
  } // next depth
leave:
  retMSP = msp;
  retFirst = firstMove;
  msp = oldMSP; // pop move list
  pvPtr = myPV; // pop PV
  retMove = bestMoveNr ? moveStack[bestMoveNr] : 0;
  retDep = resDep - (inCheck & depth >= QSdepth) + lmr;
if(PATH) printf("return %d: %d %d (t=%d s=%d lim=%d)\n", depth, bestScore, curEval, GetTickCount(), startTime, tlim1),fflush(stdout);
  return bestScore + (bestScore < curEval);
}

void
pplist()
{
  int i, j;
  for(i=0; i<182; i++) {
	printf("%3d. %3d %3d %4d   %02x %d %d %x %3d %4d ", i, p[i].value, p[i].promo, p[i].pos, p[i].promoFlag&255, p[i].mobWeight, p[i].qval, p[i].bulk, p[i].promoGain, p[i].pst);
	for(j=0; j<8; j++) printf("  %2d", p[i].range[j]);
	if(i<2 || i>11) printf("\n"); else printf("  %02x\n", fireFlags[i-2]&255);
  }
  printf("last: %d / %d\nroyal %d / %d\n", last[WHITE], last[BLACK], royal[WHITE], royal[BLACK]);
}

void
pboard (int *b)
{
  int i, j;
  for(i=BH+2; i>-4; i--) {
    printf("#");
    for(j=-3; j<BH+3; j++) printf("%4d", b[BW*i+j]);
    printf("\n");
  }
}

void
pbytes (unsigned char *b)
{
  int i, j;
  for(i=BH-1; i>=0; i--) {
    for(j=0; j<BH; j++) printf("%3x", b[BW*i+j]);
    printf("\n");
  }
}

void
pmap (int *m, int col)
{
  int i, j;
  for(i=BH-1; i>=0; i--) {
    printf("#");
    for(j=0; j<BH; j++) printf("%10o", m[2*(BW*i+j)+col]);
    printf("\n");
  }
}

void
pmoves(int start, int end)
{
  int i, m, f, t;
  printf("# move stack from %d to %d\n", start, end);
  for(i=start; i<end; i++) {
    m = moveStack[i];
    f = m>>SQLEN & SQUARE;
    t = m & SQUARE;
    printf("# %3d. %08x %3d-%3d %s\n", i, m, f, t, MoveToText(m, 0));
  }
}

    /********************************************************/
    /* Example of a WinBoard-protocol driver, by H.G.Muller */
    /********************************************************/

    #include <stdio.h>

    // four different constants, with values for WHITE and BLACK that suit your engine
    #define NONE    3
    #define ANALYZE 4

    // some value that cannot occur as a valid move
    #define INVALID 0

    // some parameter of your engine
    #define MAXMOVES 2000 /* maximum game length  */
    #define MAXPLY   60   /* maximum search depth */

    #define OFF 0
    #define ON  1

typedef Move MOVE;

    int moveNr;              // part of game state; incremented by MakeMove
    MOVE gameMove[MAXMOVES]; // holds the game history

    // Some routines your engine should have to do the various essential things
    int  MakeMove2(int stm, MOVE move);      // performs move, and returns new side to move
    void UnMake2(MOVE move);                 // unmakes the move;
    int  Setup2(char *fen);                  // sets up the position from the given FEN, and returns the new side to move
    void SetMemorySize(int n);              // if n is different from last time, resize all tables to make memory usage below n MB
    char *MoveToText(MOVE move, int m);     // converts the move from your internal format to text like e2e2, e1g1, a7a8q.
    MOVE ParseMove(char *moveText);         // converts a long-algebraic text move to your internal move format
    int  SearchBestMove(MOVE *move, MOVE *ponderMove);
    void PonderUntilInput(int stm);         // Search current position for stm, deepening forever until there is input.

UndoInfo undoInfo;
int sup0, sup1, sup2; // promo suppression squares
int lastLift, lastPut;

int
InCheck ()
{
  int k = p[royal[stm]].pos;
  if( k == ABSENT) k = p[royal[stm] + 2].pos;
  else if(p[royal[stm] + 2].pos != ABSENT) k = ABSENT; // two kings is no king...
  if( k != ABSENT) {
    MapFromScratch(attacks);
    if(attacks[2*k + 1 - stm]) return 1;
  }
  return 0;
}

int
MakeMove2 (int stm, MOVE move)
{
  int i, inCheck = InCheck();
  FireSet(&undoInfo);
  sup0 = sup1; sup1 = sup2;
  sup2 = MakeMove(move, &undoInfo);
  if(chuFlag && p[undoInfo.victim].value == LVAL && p[undoInfo.piece].value != LVAL) sup2 |= PROMOTE;
  rootEval = -rootEval - undoInfo.booty;
  for(i=0; i<200; i++) repStack[i] = repStack[i+1], checkStack[i] = checkStack[i+1];
  repStack[199] = hashKeyH, checkStack[199] = inCheck;
printf("# makemove %08x %c%d %c%d\n", move, sup1%BW+'a', sup1/BW, sup2%BW+'a', sup2/BW);
  return stm ^ WHITE;
}

void
UnMake2 (MOVE move)
{
  int i;
  rootEval = -rootEval - undoInfo.booty;
  UnMake(&undoInfo);
  for(i=200; i>0; i--) repStack[i] = repStack[i-1], checkStack[i] = checkStack[i-1];
  sup2 = sup1; sup1 = sup0;
}

char fenNames[] = "RV....DKDEFL..DHGB......SMLNKN..FK....BT..VM..PH..LN"; // pairs of char
char fenPromo[] = "WLDHSMSECPB R HFDE....WHFB..LNG ..DKVMFS..FO..FK...."; // pairs of char

char *
Convert (char *fen)
{
  char *p = fenArray, *q, *rows[36], tmp[4000];
  int n=0;
  printf("# convert FEN '%s'\n", fen);
  q = strchr(fen, ' '); if(q) *q = 0; q = fen;
  do { rows[n++] = q; q = strchr(q, '/'); if(!q) break; *q++ = 0; } while(1);
  *tmp = 0;
  while(--n >= 0) { strcat(tmp, rows[n]); if(n) strcat(tmp, "/"); }
  fen = tmp;
  printf("# flipped FEN '%s'\n", fen);
  while(*fen) {
    if(*fen == ' ') { *p = 0; break; }
    if(n=atoi(fen)) fen++; // digits read
    if(n > 9) fen++; // double digit
    while(n-- > 0) *p++ = '.'; // expand to empty squares
    if(currentVariant == V_LION && (*fen == 'L' || *fen == 'l')) *fen += 'Z' - 'L'; // L in Mighty-Lion Chess changed in Z for Lion
    if(isalpha(*fen)) {
      char *table = fenNames;
      n = *fen > 'Z' ? 'a' - 'A' : 0;
      if((currentVariant == V_CHESS || currentVariant == V_SHATRANJ || currentVariant == V_LION ||
          currentVariant == V_MAKRUK || currentVariant == V_SHO) && *fen - n == 'N' // In Chess N is Knight, not Lion
           || table[2* (*fen - 'A' - n)] == '.') *p++ = *fen; else {
        *p++ = ':';
        *p++ = table[2* (*fen - 'A' - n)] + n;
        *p++ = table[2* (*fen - 'A' - n)+1] + n;
      }
    } else *p++ = *fen;
    if(!*fen) break;
    fen++;
  }
  *p = '\0';
  printf("# converted FEN '%s'\n", fenArray);
  return fenArray;
}

int
Setup2 (char *fen)
{
  int stm = WHITE;
  if(fen) {
    char *q = strchr(fen, '\n');
    if(q) *q = 0;
    if(q = strchr(fen, ' ')) stm = (q[1] == 'b' ? BLACK : WHITE); // fen contains color field
    if(strchr(fen, '.') || strchr(fen, ':')) array = fen; else array = Convert(fen);
  }
  rootEval = promoDelta = filling = cnt50 = moveNr = 0;
  SetUp(array, currentVariant);
  strcpy(startPos, array);
  sup0 = sup1 = sup2 = ABSENT;
  hashKeyH = hashKeyL = 87620895*currentVariant + !!fen;
  return stm;
}

void
SetMemorySize (int n)
{
#ifdef HASH
  static HashBucket *realHash;
  static intptr_t oldSize;
  intptr_t l, m = 1;
  while(m*sizeof(HashBucket) <= n*512UL) m <<= 1; // take largest power-of-2 that fits
  if(m != oldSize) {
    if(oldSize) free(realHash);
    hashMask = m*1024 - 1; oldSize = m;
    realHash = malloc(m*1024*sizeof(HashBucket) + 64);
    l = (intptr_t) realHash; hashTable = (HashBucket*) (l & ~63UL); // align with cache line
  }
#endif
}

char *
MoveToText (MOVE move, int multiLine)
{
  static char buf[50];
  int f = move>>SQLEN & SQUARE, g = f, t = move & SQUARE;
  char *promoChar = "";
  if(f == t) { sprintf(buf, "@@@@"); return buf; } // null-move notation in WB protocol
  buf[0] = '\0';
  if(t >= SPECIAL) {
   if(t < CASTLE) { // castling is printed as a single move, implying its side effects
    int e = f + epList[t - SPECIAL];
//    printf("take %c%d\n", e%BW+'a', e/BW+ONE);
    sprintf(buf, "%c%d%c%d,", f%BW+'a', f/BW+ONE, e%BW+'a', e/BW+ONE); f = e;
    if(multiLine) printf("move %s\n", buf), buf[0] = '\0';
    if(ep2List[t - SPECIAL]) {
      e = g + ep2List[t - SPECIAL];
//      printf("take %c%d\n", e%BW+'a', e/BW+ONE);
      sprintf(buf+strlen(buf), "%c%d%c%d,", f%BW+'a', f/BW+ONE, e%BW+'a', e/BW+ONE); f = e;
    if(multiLine) printf("move %s\n", buf), buf[0] = '\0';
    }
   }
    t = g + toList[t - SPECIAL];
  }
  if(move & PROMOTE) promoChar = currentVariant == V_MAKRUK ? "m" : repDraws ? "q" : "+";
  sprintf(buf+strlen(buf), "%c%d%c%d%s", f%BW+'a', f/BW+ONE, t%BW+'a', t/BW+ONE,  promoChar);
  return buf;
}

int
ReadSquare (char *p, int *sqr)
{
  int i=0, f, r;
  f = p[0] - 'a';
  r = atoi(p + 1) - ONE;
  *sqr = r*BW + f;
  return 2 + (r + ONE > 9);
}

int listStart, listEnd;
char boardCopy[BSIZE];

void
ListMoves ()
{ // create move list on move stack
  int i;
  for(i=0; i< BSIZE; i++) boardCopy[i] = !!board[i];
MapFromScratch(attacks);
  postThinking--; repCnt = 0; tlim1 = tlim2 = tlim3 = 1e8; abortFlag = msp = 0;
  Search(-INF-1, INF+1, 0, QSdepth+1, 0, sup1 & ~PROMOTE, sup2, INF);
  postThinking++;

  listStart = retFirst; msp = retMSP;
  if(currentVariant == V_LION) GenCastlings(); listEnd = msp;
}

MOVE
ParseMove (char *moveText)
{
  int i, j, f, t, t2, e, ret, deferred=0;
  char c = moveText[0];
  moveText += ReadSquare(moveText, &f);
  moveText += ReadSquare(moveText, &t); t2 = t;
  if(*moveText == ',') {
    moveText++;
    moveText += ReadSquare(moveText, &e);
    if(e != t) return INVALID; // must continue with same piece
    e = t;
    moveText += ReadSquare(moveText, &t);
    for(i=0; i<8; i++) if(f + kStep[i] == e) break;
    if(i >= 8) return INVALID; // this rejects Lion Dog 2+1 and 2-1 moves!
    for(j=0; j<8; j++) if(e + kStep[j] == t) break;
    if(j >= 8) return INVALID; // this rejects Lion Dog 1+2 moves!
    t2 = SPECIAL + 8*i + j;
  } else if(chessFlag && board[f] != EMPTY && p[board[f]].value == pVal && board[t] == EMPTY) { // Pawn to empty, could be e.p.
      if(t == f + BW + 1) t2 = SPECIAL + 16; else
      if(t == f + BW - 1) t2 = SPECIAL + 48; else
      if(t == f - BW + 1) t2 = SPECIAL + 20; else
      if(t == f - BW - 1) t2 = SPECIAL + 52; // fake double-move
  } else if(currentVariant == V_LION && board[f] != EMPTY && p[board[f]].value == 280 && (t-f == 2 || f-t == 2)) { // castling
      if(t == f+2 && f < BW) t2 = CASTLE;     else
      if(t == f-2 && f < BW) t2 = CASTLE + 1; else
      if(t == f+2 && f > BW) t2 = CASTLE + 2; else
      if(t == f-2 && f > BW) t2 = CASTLE + 3;
  }
  ret = f<<SQLEN | t2;
  if(*moveText != '\n' && *moveText != '=') ret |= PROMOTE;
printf("# suppress = %c%d\n", sup1%BW+'a', sup1/BW);
//  ListMoves();
  for(i=listStart; i<listEnd; i++) {
    if(moveStack[i] == INVALID) continue;
    if(c == '@' && (moveStack[i] & SQUARE) == (moveStack[i] >> SQLEN & SQUARE)) break; // any null move matches @@@@
    if((moveStack[i] & (PROMOTE | DEFER-1)) == ret) break;
    if((moveStack[i] & DEFER-1) == ret) deferred = i; // promoted version of entered non-promotion is legal
  }
printf("# moveNr = %d in {%d,%d}\n", i, listStart, listEnd);
  if(i>=listEnd) { // no exact match
    if(deferred) { // but maybe non-sensical deferral
      int flags = p[board[f]].promoFlag;
printf("# deferral of %d\n", deferred);
      i = deferred; // in any case we take that move
      if(!(flags & promoBoard[t] & (CANT_DEFER | LAST_RANK))) { // but change it into a deferral if that is allowed
	moveStack[i] &= ~PROMOTE;
	if(p[board[f]].value == 40) p[board[f]].promoFlag &= LAST_RANK; else
	if(!(flags & promoBoard[f])) moveStack[i] |= DEFER; // came from outside zone, so essential deferral
      }
    }
    if(i >= listEnd) {
      for(i=listStart; i<listEnd; i++) printf("# %d. %08x %08x %s\n", i-50, moveStack[i], ret, MoveToText(moveStack[i], 0));
      reason = NULL;
      for(i=0; i<repCnt; i++) {if((repeatMove[i] & 0xFFFFFF) == ret) {
        if(repeatMove[i] & 1<<24) reason = (repeatMove[i] & 1<<25 ? "Distant capture of protected Lion" : "Counterstrike against Lion");
        else reason = "Repeats earlier position";
        break;
      }
 printf("# %d. %08x %08x %s\n", i, repeatMove[i], ret, MoveToText(repeatMove[i], 0));
}
    }
  }
  return (i >= listEnd ? INVALID : moveStack[i]);
}

void
Highlight(char *coords)
{
  int i, j, n, sqr, cnt=0;
  char b[BSIZE], buf[2000], *q;
  for(i=0; i<bsize; i++) b[i] = 0;
  ReadSquare(coords, &sqr);
//  ListMoves();
  for(i=listStart; i<listEnd; i++) {
    if(sqr == (moveStack[i]>>SQLEN & SQUARE)) {
      int t = moveStack[i] & SQUARE;
      if(t >= CASTLE) t = toList[t - SPECIAL]; else  // decode castling
      if(t >= SPECIAL) {
	int e = sqr + epList[t - SPECIAL]; // decode
	b[e] = 'C';
	continue;
      }
      if(!b[t]) b[t] = (!boardCopy[t] ? 'Y' : 'R'); cnt++;
      if(moveStack[i] & PROMOTE) b[t] = 'M';
    }
  }
  if(!cnt) { // no moves from given square
    if(sqr != lastPut) return; // refrain from sending empty FEN
    // we lifted a piece for second leg of move
    for(i=listStart; i<listEnd; i++) {
      if(lastLift == (moveStack[i]>>SQLEN & SQUARE)) {
	int e, t = moveStack[i] & SQUARE;
	if(t < SPECIAL) continue; // only special moves
	e = lastLift + epList[t - SPECIAL]; // decode
	t = lastLift + toList[t - SPECIAL];
	if(e != sqr) continue;
	b[t] = (!boardCopy[t] ? 'Y' : 'R'); cnt++;
      }
    }
    if(!cnt) return;
  } else lastLift = sqr; // remember
  lastPut = ABSENT;
  q = buf;
  for(i=BH-1; i>=0; i--) {
    for(j=0; j<BH; j++) {
      n = BW*i + j;
      if(b[n]) *q++ = b[n]; else {
	if(q > buf && q[-1] <= '9' && q[-1] >= '0') {
	  q[-1]++;
	  if(q[-1] > '9') {
	    if(q > buf+1 && q[-2] <= '9' && q[-2] >= '0') q[-2]++; else q[-1] = '1', *q++ = '0';
	  }
	} else *q++ = '1';
      }
    }
    *q++ = '/';
  }
  q[-1] = 0;
  printf("highlight %s\n", buf);
}

int timeLeft;                            // timeleft on engine's clock
int mps, timeControl, inc, timePerMove;  // time-control parameters, to be used by Search
char inBuf[8000], command[80], ponderMoveText[20];

void
SetSearchTimes (int timeLeft)
{
  int targetTime, movesLeft = BW*BH/4 + 20;
  if(mps) movesLeft = mps - (moveNr>>1)%mps;
  targetTime = (timeLeft - 1000*inc) / (movesLeft + 2) + 1000 * inc;
  if(moveNr < 30) targetTime *= 0.5 + moveNr/60.; // speedup in opening
  if(timePerMove > 0) targetTime = 0.4*timeLeft, movesLeft = 1;
  tlim1 = 0.4*targetTime;
  tlim2 = 2.4*targetTime;
  tlim3 = 5*timeLeft / (movesLeft + 4.1);
printf("# limits %d, %d, %d mode = %d\n", tlim1, tlim2, tlim3, abortFlag);
}

int
SearchBestMove (MOVE *move, MOVE *ponderMove)
{
  int score, i;
printf("# SearchBestMove\n");
  startTime = GetTickCount();
  nodes = 0;
printf("# s=%d\n", startTime);fflush(stdout);
MapFromScratch(attacks);
  retMove = INVALID; repCnt = 0;
  score = Search(-INF-1, INF+1, rootEval, maxDepth + QSdepth, 0, sup1, sup2, INF);
  *move = retMove;
  *ponderMove = pv[1];
printf("# best=%s\n", MoveToText(pv[0],0));
printf("# ponder=%s\n", MoveToText(pv[1],0));
  return score;
}


    int TakeBack(int n)
    { // reset the game and then replay it to the desired point
      int last, stm;
      last = moveNr - n; if(last < 0) last = 0;
      Init(SAME); stm = Setup2(startPos);
printf("# setup done");fflush(stdout);
      for(moveNr=0; moveNr<last; moveNr++) stm = MakeMove2(stm, gameMove[moveNr]),printf("make %2d: %x\n", moveNr, gameMove[moveNr]);
      return stm;
    }

    void PrintResult(int stm, int score)
    {
      char tail[100];
      if(reason) sprintf(tail, " {%s}", reason); else *tail = 0;
      if(score == 0) printf("1/2-1/2%s\n", tail);
      if(score > 0 && stm == WHITE || score < 0 && stm == BLACK) printf("1-0%s\n", tail);
      else printf("0-1%s\n", tail);
    }

    void GetLine(int root)
    {

      int i, c;
      while(1) {
        // wait for input, and read it until we have collected a complete line
        for(i = 0; (inBuf[i] = c = getchar()) != '\n'; i++) if(c == EOF || i>7997) exit(0);
        inBuf[i+1] = 0;

        // extract the first word
        sscanf(inBuf, "%s", command);
printf("# in (mode = %d,%d): %s\n", root, abortFlag, command);
        if(!strcmp(command, "otim"))    { continue; } // do not start pondering after receiving time commands, as move will follow immediately
        if(!strcmp(command, "time"))    { sscanf(inBuf, "time %d", &timeLeft); continue; }
        if(!strcmp(command, "put"))     { ReadSquare(inBuf+4, &lastPut); continue; }  // ditto
        if(!strcmp(command, "."))       { inBuf[0] = 0; return; } // ignore for now
        if(!strcmp(command, "hover"))   { inBuf[0] = 0; return; } // ignore for now
        if(!strcmp(command, "lift"))    { inBuf[0] = 0; Highlight(inBuf+5); return; } // treat here
        if(!root && !strcmp(command, "usermove")) {
printf("# move = %s#ponder = %s", inBuf+9, ponderMoveText);
          abortFlag = !!strcmp(inBuf+9, ponderMoveText);
          if(!abortFlag) { // ponder hit, continue as time-based search
printf("# ponder hit\n");
            SetSearchTimes(10*timeLeft + GetTickCount() - startTime); // add time we already have been pondering to total
            if(lastRootIter > tlim1) abortFlag = 2; // abort instantly if we are in iteration we should not have started
            inBuf[0] = 0; ponderMove = INVALID;
            return;
          }
        }
        abortFlag = 1;
        return;
      }
    }

    void
    TerminationCheck()
    {
      if(abortFlag < 0) { // check for input
        if(InputWaiting()) GetLine(0); // read & examine input command
      } else {        // check for time
        if(GetTickCount() - startTime > tlim3) abortFlag = 2;
      }
    }

    main()
    {
      int engineSide=NONE;                // side played by engine
      MOVE move;
      int i, score, curVarNr;

      setvbuf(stdin, NULL, _IOLBF, 1024); // buffering more than one line flaws test for pending input!

      Init(V_CHU); // Chu
      seed = startTime = GetTickCount(); moveNr = 0; // initialize random

      while(1) { // infinite loop

        fflush(stdout);                   // make sure everything is printed before we do something that might take time
        *inBuf = 0; if(moveNr >= 20) randomize = OFF;
//if(moveNr >20) printf("resign\n");

#ifdef HASH
	if(hashMask)
#endif
        if(listEnd == 0) ListMoves();   // always maintain a list of legal moves in root position
        abortFlag = -(ponder && WHITE+BLACK-stm == engineSide && moveNr); // pondering and opponent on move
        if(stm == engineSide || abortFlag && ponderMove) {      // if it is the engine's turn to move, set it thinking, and let it move
printf("# start search: stm=%d engine=%d (flag=%d)\n", stm, engineSide, abortFlag);
          if(abortFlag) {
            stm = MakeMove2(stm, ponderMove);                           // for pondering, play speculative move
            gameMove[moveNr++] = ponderMove;                            // remember in game
            sprintf(ponderMoveText, "%s\n", MoveToText(ponderMove, 0)); // for detecting ponder hits
printf("# ponder move = %s", ponderMoveText);
          } else SetSearchTimes(10*timeLeft);                           // for thinking, schedule end time
pboard(board);
          score = SearchBestMove(&move, &ponderMove);
          if(abortFlag == 1) { // ponder search was interrupted (and no hit)
            UnMake2(INVALID); moveNr--; stm ^= WHITE;    // take ponder move back if we made one
            abortFlag = 0;
          } else
          if(move == INVALID) {         // game apparently ended
            int kcapt = 0, xstm = stm ^ WHITE, king, k = p[king=royal[xstm]].pos;
            if( k != ABSENT) { // test if King capture possible
              if(attacks[2*k + stm]) {
                if( p[king + 2].pos == ABSENT ) kcapt = 1; // we have an attack on his only King
              }
            } else { // he has no king! Test for attacks on Crown Prince
              k = p[king + 2].pos;
              if(attacks[2*k + stm]) kcapt = 1; // we have attack on Crown Prince
            }
            if(kcapt) { // print King capture before claiming
              GenCapts(k, 0);
              printf("move %s\n", MoveToText(moveStack[msp-1], 1));
              reason = "king capture";
            } else reason = "resign";
            engineSide = NONE;          // so stop playing
            PrintResult(stm, score);
          } else {
            MOVE f, pMove = move;
            if((move & SQUARE) >= SPECIAL && p[board[f = move>>SQLEN & SQUARE]].value == pVal) { // e.p. capture
              pMove = move & ~SQUARE | f + toList[(move & SQUARE) - SPECIAL]; // print as a single move
            }
            stm = MakeMove2(stm, move);  // assumes MakeMove returns new side to move
            gameMove[moveNr++] = move;   // remember game
            printf("move %s\n", MoveToText(pMove, 1));
            listEnd = 0;
            continue;                    // go check if we should ponder
          }
        } else
        if(engineSide == ANALYZE || abortFlag) { // in analysis, we always ponder the position
            Move dummy;
            *ponderMoveText = 0; // forces miss on any move
            abortFlag = -1;      // set pondering
	    pvCuts = noCut;
            SearchBestMove(&dummy, &dummy);
            abortFlag = pvCuts = 0;
        }

        fflush(stdout);         // make sure everything is printed before we do something that might take time
        if(!*inBuf) GetLine(1); // takes care of time and otim commands

        // recognize the command,and execute it
        if(!strcmp(command, "quit"))    { break; } // breaks out of infinite loop
        if(!strcmp(command, "force"))   { engineSide = NONE;    continue; }
        if(!strcmp(command, "analyze")) { engineSide = ANALYZE; continue; }
        if(!strcmp(command, "exit"))    { engineSide = NONE;    continue; }
        if(!strcmp(command, "level"))   {
          int min, sec=0;
          sscanf(inBuf, "level %d %d %d", &mps, &min, &inc) == 3 ||  // if this does not work, it must be min:sec format
          sscanf(inBuf, "level %d %d:%d %d", &mps, &min, &sec, &inc);
          timeControl = 60*min + sec; timePerMove = -1;
          continue;
        }
        if(!strcmp(command, "protover")){
          for(i=0; variants[i].boardWidth; i++)
          printf("%s%s", (i ? "," : "feature variants=\""), variants[i].name); printf("\"\n");
          printf("feature ping=1 setboard=1 colors=0 usermove=1 memory=1 debug=1 sigint=0 sigterm=0\n");
          printf("feature myname=\"HaChu " VERSION "\" highlight=1\n");
          printf("feature option=\"Full analysis PV -check 1\"\n"); // example of an engine-defined option
          printf("feature option=\"Allow repeats -check 0\"\n");
          printf("feature option=\"Promote on entry -check 0\"\n");
          printf("feature option=\"Okazaki rule -check 0\"\n");
          printf("feature option=\"Resign -check 0\"\n");           // 
          printf("feature option=\"Contempt -spin 0 -200 200\"\n"); // and another one
          printf("feature option=\"Tsume -combo no /// Sente mates /// Gote mates\"\n");
          printf("feature done=1\n");
          continue;
        }
        if(!strcmp(command, "option")) { // setting of engine-define option; find out which
          if(sscanf(inBuf+7, "Full analysis PV=%d", &noCut)  == 1) continue;
          if(sscanf(inBuf+7, "Allow repeats=%d", &allowRep)  == 1) continue;
          if(sscanf(inBuf+7, "Resign=%d",   &resign)         == 1) continue;
          if(sscanf(inBuf+7, "Contempt=%d", &contemptFactor) == 1) continue;
          if(sscanf(inBuf+7, "Okazaki rule=%d", &okazaki)    == 1) continue;
          if(sscanf(inBuf+7, "Promote on entry=%d", &entryProm) == 1) continue;
          if(sscanf(inBuf+7, "Tsume=%s", command) == 1) {
	    if(!strcmp(command, "no"))    tsume = 0; else
	    if(!strcmp(command, "Sente")) tsume = 1; else
	    if(!strcmp(command, "Gote"))  tsume = 2;
	    continue;
	  }
          continue;
        }
        if(!strcmp(command, "sd"))      { sscanf(inBuf, "sd %d", &maxDepth);    continue; }
        if(!strcmp(command, "st"))      { sscanf(inBuf, "st %d", &timePerMove); continue; }

        if(!strcmp(command, "memory"))  { SetMemorySize(atoi(inBuf+7)); continue; }
        if(!strcmp(command, "ping"))    { printf("pong%s", inBuf+4); continue; }
    //  if(!strcmp(command, ""))        { sscanf(inBuf, " %d", &); continue; }
        if(!strcmp(command, "easy"))    { ponder = OFF; continue; }
        if(!strcmp(command, "hard"))    { ponder = ON;  continue; }
        if(!strcmp(command, "go"))      { engineSide = stm;  continue; }
        if(!strcmp(command, "post"))    { postThinking = ON; continue; }
        if(!strcmp(command, "nopost"))  { postThinking = OFF;continue; }
        if(!strcmp(command, "random"))  { randomize = ON;    continue; }
        if(!strcmp(command, "hint"))    { if(ponderMove != INVALID) printf("Hint: %s\n", MoveToText(ponderMove, 0)); continue; }
        if(!strcmp(command, "book"))    {  continue; }
	// non-standard commands
        if(!strcmp(command, "p"))       { pboard(board); continue; }
        if(!strcmp(command, "f"))       { pbytes(fireBoard); continue; }
        if(!strcmp(command, "w"))       { MapOneColor(WHITE, last[WHITE], attacks); pmap(attacks, WHITE); continue; }
        if(!strcmp(command, "b"))       { MapOneColor(BLACK, last[BLACK], attacks); pmap(attacks, BLACK); continue; }
        if(!strcmp(command, "l"))       { pplist(); continue; }
        // ignored commands:
        if(!strcmp(command, "xboard"))  { continue; }
        if(!strcmp(command, "computer")){ comp = 1; continue; }
        if(!strcmp(command, "name"))    { continue; }
        if(!strcmp(command, "ics"))     { continue; }
        if(!strcmp(command, "accepted")){ continue; }
        if(!strcmp(command, "rejected")){ continue; }
        if(!strcmp(command, "result"))  { engineSide = NONE; continue; }
        if(!strcmp(command, "hover"))   { continue; }
        if(!strcmp(command, ""))  {  continue; }
        if(!strcmp(command, "usermove")){
          int move = ParseMove(inBuf+9);
pboard(board);
          if(move == INVALID) {
            if(reason) printf("Illegal move {%s}\n", reason); else printf("%s\n", reason="Illegal move");
            if(comp) PrintResult(stm, -INF); // against computer: claim
          } else {
            stm = MakeMove2(stm, move);
            ponderMove = INVALID; listEnd = 0;
            gameMove[moveNr++] = move;  // remember game
          }
          continue;
        }
        ponderMove = INVALID; // the following commands change the position, invalidating ponder move
        listEnd = 0;
        if(!strcmp(command, "new"))     {
          engineSide = BLACK; Init(V_CHESS); stm = Setup2(NULL); maxDepth = MAXPLY; randomize = OFF; curVarNr = comp = 0;
          continue;
        }
        if(!strcmp(command, "variant")) {
          for(i=0; variants[i].boardWidth; i++) {
            sscanf(inBuf+8, "%s", command);
            if(!strcmp(variants[i].name, command)) {
              Init(curVarNr = i); stm = Setup2(NULL); break;
            }
	  }
          if(currentVariant == V_SHO)
            printf("setup (PNBRLSE..G.+++++++Kpnbrlse..g.+++++++k) 9x9+0_shogi lnsgkgsnl/1r2e2b1/ppppppppp/9/9/9/PPPPPPPPP/1B2E2R1/LNSGKGSNL w 0 1\n");
	  repStack[199] = hashKeyH, checkStack[199] = 0;
          continue;
        }
        if(!strcmp(command, "setboard")){ engineSide = NONE;  Init(curVarNr); stm = Setup2(inBuf+9); continue; }
        if(!strcmp(command, "undo"))    { stm = TakeBack(1); continue; }
        if(!strcmp(command, "remove"))  { stm = TakeBack(2); continue; }
        printf("Error: unknown command\n");
      }
    }

