/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/

#include <time.h>

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

typedef unsigned int Move;

char *MoveToText(Move move, int m);     // from WB driver
void pmap(int color);
void pboard(int *b);
void pbytes(unsigned char *b);
int myRandom();

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
#define RAYS 8

typedef struct {
  char *name, *promoted;
  int value;
  signed char range[RAYS];
  char bulk;
  char ranking;
  int whiteKey, blackKey;
} PieceDesc;

typedef struct {
  int from, to, piece, victim, new, booty, epSquare, epVictim[9], ep2Square, revMoveCount;
  int savKeyL, savKeyH, gain, loss, filling, saveDelta;
  char fireMask;
} UndoInfo;

char *array, *IDs, fenArray[4000], startPos[4000], *reason, checkStack[300];
int bFiles, bRanks, zone, currentVariant, chuFlag, tenFlag, chessFlag, repDraws, stalemate;
int tsume, pvCuts, allowRep, entryProm=1, okazaki, pVal;
int stm, xstm, hashKeyH=1, hashKeyL=1, framePtr, msp, nonCapts, rootEval, filling, promoDelta;
int retFirst, retMSP, retDep, pvPtr, level, cnt50, mobilityScore;
int nodes, startTime, lastRootMove, lastRootIter, tlim1, tlim2, tlim3, repCnt, comp, abortFlag;
Move ponderMove;
#define LEVELS 200
Move retMove, moveStack[20000], path[100], repStack[LEVELS+(50*2)], pv[1000], repeatMove[300], killer[100][2];

      int maxDepth;                            // used by search

#define X 36 /* slider              */
#define R 37 /* jump capture        */
#define N -1 /* Knight              */
#define J -2 /* jump                */
#define I -3 /* jump + step         */
#define K -4 /* triple + range      */
#define T -5 /* linear triple move  */
#define D -6 /* linear double move  */
#define L -7 /* true Lion move      */
#define W -8 /* Werewolf move       */
#define F -9 /* Lion + 3-step       */
#define S -10 /* Lion + range        */
#define H -11 /* hook move           */
#define C -12 /* capture only       */
#define M -13 /* non-capture only   */

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
  {"SG", "G",  50, { 0,1,0,0,0,0,0,1 }, 0 }, // Stone
  { NULL }  // sentinel
};

PieceDesc waPieces[] = {
  {"TE", "",   720, { X,X,1,X,X,X,1,X }, 4 }, // Tenacious Falcon
  {"GS", "",   500, { X,0,X,0,X,0,X,0 }, 4 }, // Gliding Swallow (R)
  {"CE", "",   430, { X,3,1,1,X,1,1,3 }, 3 }, // Cloud Eagle
  {"K",  "",   410, { 1,1,1,1,1,1,1,1 }, 2 }, // Crane King (K)
  {"TF", "",   390, { I,I,0,I,I,I,0,I }, 4 }, // Treacherous Fox
  {"FF", "TE", 380, { 1,X,0,X,0,X,0,X }, 4 }, // Flying Falcon
  {"RF", "",   290, { X,1,1,0,X,0,1,1 }, 3 }, // Raiding Falcon
  {"SW", "GS", 260, { 1,0,X,0,1,0,X,0 }, 6 }, // Swallow's Wing (SM)
  {"PO", "",   260, { 1,1,1,1,1,1,1,1 }, 2 }, // Plodding Ox (K)
  {"RR", "TF", 260, { X,1,0,1,1,1,0,1 }, 2 }, // Running Rabit
  {"RB", "",   240, { 1,1,1,1,0,1,1,1 }, 2 }, // Roaming Boar
  {"HH", "",   220, { N,0,0,N,N,0,0,N }, 1 }, // Heavenly Horse
  {"VW", "PO", 220, { 1,1,1,0,1,0,1,1 }, 2 }, // Violent Wolf (G)
  {"VS", "RB", 200, { 1,1,0,1,0,1,0,1 }, 2 }, // Violent Stag (S)
  {"FG", "SW", 190, { 1,1,0,0,1,0,0,1 }, 2 }, // Flying Goose (C)
  {"CM", "VS", 175, { 1,1,0,0,1,0,0,1 }, 2 }, // Climbing Monkey (C)
  {"LH", "HH", 170, { X,0,0,0,2,0,0,0 }, 1 }, // Liberated Horse
  {"BD", "VW", 150, { 0,1,1,0,1,0,1,1 }, 2 }, // Blind Dog
  {"OC", "PO", 150, { X,0,0,0,0,0,0,0 }, 1 }, // Oxcart (L)
  {"FC", "RF", 130, { 0,1,1,0,0,0,1,1 }, 2 }, // Flying Cock
  {"SO", "CE", 115, { 1,0,0,1,0,1,0,0 }, 2 }, // Swooping Owl
  {"SC", "FF", 105, { 1,0,0,1,0,1,0,0 }, 2 }, // Strutting Crow
  {"P",  "VW",  80, { 1,0,0,0,0,0,0,0 }, 2 }, // Sparrow Pawn (P)
  { NULL }  // sentinel
};

PieceDesc ddPieces[] = {
  {"HM", "",   10, { H,0,H,0,H,0,H,0 } }, // Hook Mover H!
  {"LO", "",   10, { 1,H,1,H,1,H,1,H } }, // Long-Nosed Goblin G!
  {"OK", "LO", 10, { 2,1,2,0,2,0,2,1 } }, // Old Kite K'
  {"PS", "HM", 10, { J,0,1,J,0,J,1,0 } }, // Poisonous Snake S'
  {"FF", "",   10, { F,F,F,F,F,F,F,F } }, // Furious Fiend +L!
  {"GE", "",   10, { 3,3,5,5,3,5,5,3 } }, // Great Elephant +W!
  {"We", "LD", 10, { 1,1,2,0,1,0,2,1 } }, // Western Barbarian W'
  {"Ea", "LN", 10, { 2,1,1,0,2,0,1,1 } }, // Eastern Barbarian E'
  {"No", "FE", 10, { 0,2,1,1,0,1,1,2 } }, // Northern Barbarian N'
  {"So", "WE", 10, { 0,1,1,2,0,2,1,1 } }, // Southern Barbarian S'
  {"FE", "",   10, { 2,X,2,2,2,2,2,X } }, // Fragrant Elephant +N'
  {"WE", "",   10, { 2,2,2,X,2,X,2,2 } }, // White Elephant +S'
  {"FT", "",   10, { X,X,5,0,X,0,5,X } }, // Free Dream-Eater +W
  {"FR", "",   10, { 5,X,X,0,5,0,X,X } }, // Free Demon +U
  {"WB", "FT", 10, { 2,X,X,X,2,X,X,X } }, // Water Buffalo W
  {"RU", "FR", 10, { X,X,X,X,0,X,X,X } }, // Rushing Bird U
  {"SB", "",   10, { X,X,2,2,2,2,2,X } }, // Standard Bearer +N
  {"FH", "FK", 10, { 1,2,1,0,1,0,1,2 } }, // Flying Horse  H'
  {"NK", "SB", 10, { 1,1,1,1,1,1,1,1 } }, // Neighbor King N
  {"RG", "",   10, { 1,1,0,1,1,1,1,1 } }, // Right General R'
  {"LG", "",   10, { 1,1,1,1,1,1,0,1 } }, // Left General L'
  {"BM", "MW", 10, { 0,1,1,1,0,1,1,1 } }, // Blind Monkey
  {"DO", "",   10, { 2,5,2,5,2,5,2,5 } }, // Dove
  {"EB", "DO", 10, { 2,0,2,0,0,0,2,0 } }, // Enchanted Badger B'
  {"EF", "SD", 10, { 0,2,0,0,2,0,0,2 } }, // Enchanted Fox X'
  {"RA", "",   10, { X,0,X,1,X,1,X,0 } }, // Racing Chariot A
  {"SQ", "",   10, { X,1,X,0,X,0,X,1 } }, // Square Mover Q'
  {"PR", "SQ", 10, { 1,1,2,1,0,1,2,1 } }, // Prancing Stag
  {"WT", "",   10, { X,1,2,0,X,0,2,X } }, // White Tiger T!
  {"BD", "",   10, { 2,X,X,0,2,0,X,1 } }, // Blue Dragon D!
  {"HD", "",   10, { X,0,0,0,1,0,0,0 } }, // Howling Dog D'
  {"VB", "",   10, { 0,2,1,0,0,0,1,2 } }, // Violent Bear V
  {"ST", "",   10, { 2,1,0,0,2,0,0,1 } }, // Savage Tiger T'
  {"W",  "",   10, { 0,2,0,0,0,0,0,2 } }, // Wood General V'
  {"CS", "DH",  70, { 0,1,0,1,0,1,0,1 } }, // Cat Sword C'
  {"FD", "DK", 150, { 0,2,0,2,0,2,0,2 } }, // Flying Dragon F'
  {"LD", "GE", 10, { T,T,T,T,T,T,T,T } }, // Lion Dog W!
  {"AB", "",   10, { 1,0,1,0,1,0,1,0 } }, // Angry Boar A'
  {"EW", "",   10, { 1,1,1,0,0,0,1,1 } }, // Evil Wolf
  {"SD", "",   10, { 5,2,5,2,5,2,5,2 } }, // She-Devil
  {"GD", "",   10, { 2,3,X,3,2,3,X,3 } }, // Great Dragon
  {"GO", "",   10, { X,3,2,3,X,3,2,3 } }, // Golden Bird
  {"LC", "",   10, { X,0,0,X,1,0,0,X } }, // Left Chariot L'
  {"RC", "",   10, { X,X,0,0,1,X,0,0 } }, // Right Chariot R'
  // Chu pieces (but with different promotion)
  {"LN", "FF",LVAL, { L,L,L,L,L,L,L,L }, 4 }, // lion
  {"FK", "",   600, { X,X,X,X,X,X,X,X }, 4 }, // free king
  {"DK", "",   400, { X,1,X,1,X,1,X,1 }, 4 }, // dragon king
  {"DH", "",   350, { 1,X,1,X,1,X,1,X }, 4 }, // dragon horse
  {"R",  "",   300, { X,0,X,0,X,0,X,0 }, 4 }, // rook
  {"K",  "",   280, { 1,1,1,1,1,1,1,1 }, 2, 4 }, // king
  {"B",  "",   250, { 0,X,0,X,0,X,0,X }, 2 }, // bishop
  {"VM", "",   200, { X,0,1,0,X,0,1,0 }, 2 }, // vertical mover
  {"SM", "",   200, { 1,0,X,0,1,0,X,0 }, 6 }, // side mover
  {"G",  "",   151, { 1,1,1,0,1,0,1,1 }, 2 }, // gold
  {"FL", "",   150, { 1,1,0,1,1,1,0,1 }, 2 }, // ferocious leopard
  {"KN", "GD", 154, { J,1,J,1,J,1,J,1 }, 2 }, // kirin
  {"PH", "GO", 153, { 1,J,1,J,1,J,1,J }, 2 }, // phoenix
  {"RV", "",   150, { X,0,0,0,X,0,0,0 }, 1 }, // reverse chariot
  {"L",  "",   150, { X,0,0,0,0,0,0,0 }, 1 }, // lance
  {"S",  "",   100, { 1,1,0,1,0,1,0,1 }, 2 }, // silver
  {"C",  "",   100, { 1,1,0,0,1,0,0,1 }, 2 }, // copper
  {"P",  "",    40, { 1,0,0,0,0,0,0,0 }, 2 }, // pawn
  {"", "", 10, {  } }, // 
  {"", "", 10, {  } }, // 
  { NULL }  // sentinel
};

PieceDesc makaPieces[] = {
  {"DV", "TK", 10, { 0,1,0,1,0,0,1,1 } }, // Deva I'
  {"DS", "BS", 10, { 0,1,1,0,0,1,0,1 } }, // Dark Spirit J'
  {"T",  "fT", 10, { 0,1,0,0,1,0,0,1 } }, // Tile General Y
  {"CO", "fS", 10, { 1,0,0,1,1,1,0,0 } }, // Coiled Serpent S!
  {"RD", "fD", 10, { 1,0,1,1,1,1,1,0 } }, // Reclining Dragon D!
  {"CC", "WS", 10, { 0,1,1,0,1,0,1,1 } }, // Chinese Cock N'
  {"OM", "MW", 10, { 0,1,0,1,1,1,0,1 } }, // Old Monkey M'
  {"BB", "fB", 10, { 0,1,0,1,X,1,0,1 } }, // Blind Bear B'
  {"OR", "BA", 10, { 0,2,0,0,2,0,0,2 } }, // Old Rat O'
  {"LD", "G", 800, { T,T,T,T,T,T,T,T } }, // Lion Dog W!
  {"WR", "G", 10, { 0,3,1,3,0,3,1,3 } }, // Wrestler W'
  {"GG", "G", 10, { 3,1,3,0,3,0,3,1 } }, // Guardian of the Gods G'
  {"BD", "G", 10, { 0,3,1,0,1,0,1,3 } }, // Budhist Devil D'
  {"SD", "G", 10, { 5,2,5,2,5,2,5,2 } }, // She-Devil S'
  {"DY", "G", 10, { J,0,1,0,J,0,1,0 } }, // Donkey Y'
  {"CA", "G", 10, { 0,H,0,2,0,2,0,H } }, // Capricorn C!
  {"HM", "G", 10, { H,0,H,0,H,0,H,0 } }, // Hook Mover H!
  {"SF", "G", 10, { 0,1,X,1,0,1,0,1 } }, // Side Flier F!
  {"LC", "G", 10, { X,0,0,X,1,0,0,X } }, // Left Chariot L'
  {"RC", "G", 10, { X,X,0,0,1,X,0,0 } }, // Right Chariot R'
  {"fT", "", 10, { 0,X,X,X,X,X,X,X } }, // Free Tiger +T
  {"fD", "", 10, { X,0,X,X,X,X,X,0 } }, // Free Dragon +D!
  {"fG", "", 10, { X,X,X,0,X,0,X,X } }, // Free Gold +G
  {"fS", "", 10, { X,X,0,X,0,X,0,X } }, // Free Silver +S
  {"fI", "", 10, { X,X,0,0,0,0,0,X } }, // Free Iron +I
  {"fY", "", 10, { 0,X,0,0,X,0,0,X } }, // Free Tile +Y
  {"fU", "", 10, { 0,X,0,0,0,0,0,X } }, // Free Stone +U
  {"EM", "", 10, { 0,0,0,0,0,0,0,0 } }, // Emperor +K

  {"TK", "", 1300, { K,K,K,K,K,K,K,K }, 0, 6}, // Teaching King +I'
  {"BS", "", 1500, { S,S,S,S,S,S,S,S }, 0, 7}, // Budhist Spirit +J'
  {"WS", "", 10, { X,X,0,X,1,X,0,X } }, // Wizard Stork +N'
  {"MW", "", 10, { 1,X,0,X,X,X,0,X } }, // Mountain Witch +M'
  {"FF", "", 1150, { F,F,F,F,F,F,F,F } }, // Furious Fiend +L!
  {"GD", "", 10, { 2,3,X,3,2,3,X,3 } }, // Great Dragon +W!
  {"GO", "", 10, { X,3,2,3,X,3,2,3 } }, // Golden Bird +X
  {"fW", "", 10, { X,X,X,0,0,0,X,X } }, // Free Wolf +W
  {"BA", "", 10, { X,0,0,X,0,X,0,0 } }, // Bat +O'
  {"FD", "G", 150, { 0,2,0,2,0,2,0,2 }, 2 }, // Flying Dragon
  // Dai pieces with different promotion
  {"FD", "G", 150, { 0,2,0,2,0,2,0,2 }, 2 }, // Flying Dragon
  {"VO", "G", 200, { 2,0,2,0,2,0,2,0 }, 2 }, // Violent Ox
  {"EW", "fW", 80, { 1,1,1,0,0,0,1,1 }, 2 }, // Evil Wolf
  {"CS", "B",  70, { 0,1,0,1,0,1,0,1 }, 1 }, // Cat Sword
  {"AB", "FB", 60, { 1,0,1,0,1,0,1,0 }, 1 }, // Angry Boar
  {"T",  "fY", 80, { 0,1,0,0,1,0,0,1 }, 2 }, // Tile
  {"I",  "fI", 80, { 1,1,0,0,0,0,0,1 }, 2 }, // Iron
  {"N",  "G",  60, { N,0,0,0,0,0,0,N }, 0 }, // Knight
  {"SG", "fU", 50, { 0,1,0,0,0,0,0,1 }, 0 }, // Stone
  // Chu pieces (but with different promotion)
  {"LN", "FF",LVAL, { L,L,L,L,L,L,L,L }, 4 }, // lion
  {"FK", "",   600, { X,X,X,X,X,X,X,X }, 4 }, // free king
  {"FO", "",   400, { X,X,0,X,X,X,0,X }, 4 }, // flying ox (free leopard)
  {"FB", "",   400, { 0,X,X,X,0,X,X,X }, 4 }, // free boar
  {"DK", "",   400, { X,1,X,1,X,1,X,1 }, 4 }, // dragon king
  {"DH", "",   350, { 1,X,1,X,1,X,1,X }, 4 }, // dragon horse
  {"WH", "",   350, { X,X,0,0,X,0,0,X }, 3 }, // white horse (free copper)
  {"R",  "G",  300, { X,0,X,0,X,0,X,0 }, 4 }, // rook
  {"WL", "",   250, { X,0,0,X,X,X,0,0 }, 4 }, // whale (free serpent)
  {"K",  "EM", 280, { 1,1,1,1,1,1,1,1 }, 2, 4 }, // king
  {"CP", "",   270, { 1,1,1,1,1,1,1,1 }, 2, 4 }, // king
  {"B",  "G",  250, { 0,X,0,X,0,X,0,X }, 2 }, // bishop
  {"VM", "G",  200, { X,0,1,0,X,0,1,0 }, 2 }, // vertical mover
  {"SM", "G",  200, { 1,0,X,0,1,0,X,0 }, 6 }, // side mover
  {"DE", "CP", 201, { 1,1,1,1,0,1,1,1 }, 2 }, // drunk elephant
  {"BT", "fT", 152, { 0,1,1,1,1,1,1,1 }, 2 }, // blind tiger
  {"G",  "fG", 151, { 1,1,1,0,1,0,1,1 }, 2 }, // gold
  {"FL", "FO", 150, { 1,1,0,1,1,1,0,1 }, 2 }, // ferocious leopard
  {"KN", "GD", 154, { J,1,J,1,J,1,J,1 }, 2 }, // kirin
  {"PH", "GB", 153, { 1,J,1,J,1,J,1,J }, 2 }, // phoenix
  {"RV", "G",  150, { X,0,0,0,X,0,0,0 }, 1 }, // reverse chariot
  {"L",  "G",  150, { X,0,0,0,0,0,0,0 }, 1 }, // lance
  {"S",  "fS", 100, { 1,1,0,1,0,1,0,1 }, 2 }, // silver
  {"C",  "WH", 100, { 1,1,0,0,1,0,0,1 }, 2 }, // copper
  {"GB", "RV",  50, { 1,0,0,0,1,0,0,0 }, 1 }, // go between
  {"P",  "G",   40, { 1,0,0,0,0,0,0,0 }, 2 }, // pawn
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
  {"Q", "",  950, { X,X,X,X,X,X,X,X } },
  {"R", "",  500, { X,0,X,0,X,0,X,0 } },
  {"B", "",  320, { 0,X,0,X,0,X,0,X } },
  {"N", "",  300, { N,N,N,N,N,N,N,N } },
  {"K", "",  280, { 1,1,1,1,1,1,1,1 } },
  {"P", "Q",  80, { M,C,0,0,0,0,0,C } },
  { NULL }  // sentinel
};

PieceDesc lionPieces[] = {
  {"L", "", LVAL, { L,L,L,L,L,L,L,L } },
  {"Q", "",  600, { X,X,X,X,X,X,X,X } },
  {"R", "",  300, { X,0,X,0,X,0,X,0 } },
  {"K", "",  280, { 1,1,1,1,1,1,1,1 } },
  {"B", "",  190, { 0,X,0,X,0,X,0,X } },
  {"N", "",  180, { N,N,N,N,N,N,N,N } },
  {"P", "Q",  50, { M,C,0,0,0,0,0,C } },
  { NULL }  // sentinel
};

PieceDesc shatranjPieces[] = {
  {"Q", "",  150, { 0,1,0,1,0,1,0,1 } },
  {"R", "",  500, { X,0,X,0,X,0,X,0 } },
  {"B", "",   90, { 0,J,0,J,0,J,0,J } },
  {"N", "",  300, { N,N,N,N,N,N,N,N } },
  {"K", "",  280, { 1,1,1,1,1,1,1,1 } },
  {"P", "Q",  80, { M,C,0,0,0,0,0,C } },
  { NULL }  // sentinel
};

PieceDesc makrukPieces[] = {
  {"M", "",  150, { 0,1,0,1,0,1,0,1 } },
  {"R", "",  500, { X,0,X,0,X,0,X,0 } },
  {"S", "",  200, { 1,1,0,1,0,1,0,1 } }, // silver
  {"N", "",  300, { N,N,N,N,N,N,N,N } },
  {"K", "",  280, { 1,1,1,1,1,1,1,1 } },
  {"P", "M",  80, { M,C,0,0,0,0,0,C } },
  { NULL }  // sentinel
};

PieceDesc wolfPieces[] = {
  {"W", "W",1050,{ W,W,W,W,W,W,W,W }, 6, 5 }, // kludge to get extra Werewolves
  {"R", "",  500, { X,0,X,0,X,0,X,0 }, 3 },
  {"B", "",  320, { 0,X,0,X,0,X,0,X }, 1 },
  {"N", "",  300, { N,N,N,N,N,N,N,N }, 1 },
  {"K", "",  280, { 1,1,1,1,1,1,1,1 }, 0, 4 },
  {"P", "R",  80, { M,C,0,0,0,0,0,C } },
  { NULL }  // sentinel
};

char chuArray[] = "lfcsgekgscfl/a1b1txot1b1a/mvrhdqndhrvm/pppppppppppp/3i4i3"
		  "/12/12/"
		  "3I4I3/PPPPPPPPPPPP/MVRHDNQDHRVM/A1B1TOXT1B1A/LFCSGKEGSCFL";
char daiArray[] = "lnuicsgkgsciunl/a1c'1f1tet1f1c'1a/1x'1a'1wxl!ow1a'1x'1/rf'mvbhdqdhbvmf'r/"
		  "ppppppppppppppp/4p'5p'4/15/15/15/4P'5P'4/PPPPPPPPPPPPPPP/"
		  "RF'MVBHDQDHBVMF'R/1X'1A'1WOL!XW1A'1X'1/A1C'1F1TET1F1C'1A/LNUICSGKGSCIUNL";
char tenArray[] = "lnficsgekgscifnl/a1c!c!1txql!ot1c!c!1a/s'v'bhdw!d!q!h!d!w!dhbv's'/"
		  "mvrf!e!b!r!v!q!r!b!e!f!rvm/pppppppppppppppp/4d6d4/"
		  "16/16/16/16/"
		  "4D6D4/PPPPPPPPPPPPPPPP/MVRF!E!B!R!Q!V!R!B!E!F!RVM/"
		  "S'V'BHDW!D!H!Q'D!W!DHBV'S'/A1C!C!TOL!QXT1C!C!1A/LNFICSGKEGSCIFNL";
char cashewArray[]= "lh!f'dh'j'ki'qc'hg!l/t!p'w!+oogngx+xl!k'd!/r've'fst'+nt'sfw'vl'/ppppppppppppp/3d'5d'3/13/"
		    "13/13/3D'5D'3/PPPPPPPPPPPPP/L'VW'FST'+NT'SFE'VR'/D!K'L!+XXGNGO+OW!P'T!/LG!HC'QI'KJ'H'DF'H!L";
char macadArray[] = "lxcsgi'kj'gscol/1f'1w!1tet1l!1f'1/rr'g'bdh!qc!dbw'l'r/ppppppppppppp/3p'5p'3/13/"
		    "13/13/3P'5P'3/PPPPPPPPPPPPP/RL'W'BDC!QH!DBG'R'R/1F'1L!1TET1W!1F'1/LOCSGI'KJ'GSCXL";
char shoArray[]   = "lnsgkgsnl/1r2e2b1/ppppppppp/9/9/9/PPPPPPPPP/1B2E2R1/LNSGKGSNL";
char waArray[]    = "hmlcvkwgudo/1e3s3f1/ppprpppxppp/3p3p3/11/11/11/3P3P3/PPPXPPPRPPP/1F3S3E1/ODUGWKVCLMH";
char chessArray[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR";
char lionArray[]  = "rlbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RLBQKBNR";
char shatArray[]  = "rnbkqbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBKQBNR";
char thaiArray[]  = "rnsmksnr/8/pppppppp/8/8/PPPPPPPP/8/RNSKMSNR";
char wolfArray[]  = "rnbwkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBWKBNR";

// translation tables for single-(dressed-)letter IDs to multi-letter names, per variant
//                 A.B.C.D.E.F.G.H.I.J.K.L.M.N.O.P.Q.R.S.T.U.V.W.X.Y.Z.
char chuIDs[] =   "RVB C DKDEFLG DHGB..K L SMLNKNP FKR S BT..VM..PH....";
char daiIDs[] =   "RVB C DKDEFLG DHI ..K L SMN KNP FKR S BTSGVMEWPH...."  // L
                  "AB  CS    FD                  GB              VO    "  // L'
                  "                      LN                            "; // L!
char tenIDs[] =   "RVB C DKDEFLG DHI ..K L SMN KNP FKR S BT..VM..PH...."  // L
                  "..  ..D   ..                    FE  SS    VS        "  // L'
                  "  BGCSFISEHF  LH      LN        GGRG      VGWB      "; // L!
char waIDs[] =    "....FCBDCEFFFGLH....K SOCM..OCP ..RRSW..SCVSVWTF....";
char chessIDs[] = "A B ....E F ........K L M N ..P Q R S ......W ......"; // covers all chess-like variants
char makaIDs[]  = "RVB C DKDEFLG DHI ..K L SMN KNP FKR S BTSG..EWPHT .."  // L (also for Macadamia)
                  "ABBBCSBDE FDGG  DVDS  LCBMCCORGB  RCSD      WRVODY  "  // L'
                  "    CARD      HM      LN            CO    VMLD      "; // L!
char dadaIDs[]  = "RVB C DKEFFLG DHI ..K L SMNKKNP FKR S ..SGVBEWPH...."  // L (also for Cashew)
                  "ABEBCSHDEaFDPRFHLGRGOKLCOMNoORPS  RCSoST  W WeVO    "  // L'
                  "RAWB  BD    LGHM      LN        SQ    WTRUVMLD      "; // L!

typedef struct {
  int boardFiles, boardRanks, zoneDepth, varNr; // board sizes
  char *name;  // WinBoard name
  char *array; // initial position
  char *IDs;
} VariantDesc;

typedef enum { V_CHESS, V_SHO, V_CHU, V_DAI, V_DADA, V_MAKA, V_TAI, V_KYOKU, V_TENJIKU,
	       V_CASHEW, V_MACAD, V_SHATRANJ, V_MAKRUK, V_LION, V_WA, V_WOLF } Variant;

#define SAME (-1)

VariantDesc variants[] = {
  { 12, 12, 4, V_CHU,     "chu",     chuArray,  chuIDs },    // Chu
  {  8,  8, 1, V_CHESS,   "nocastle", chessArray,chessIDs }, // FIDE
  {  9,  9, 3, V_SHO,     "9x9+0_shogi", shoArray,  chuIDs },// Sho
  {  9,  9, 3, V_SHO,     "sho",     shoArray,  chuIDs },    // Sho duplicat
  { 15, 15, 5, V_DAI,     "dai",     daiArray,  daiIDs },    // Dai
  { 16, 16, 5, V_TENJIKU, "tenjiku", tenArray,  tenIDs },    // Tenjiku
  {  8,  8, 1, V_SHATRANJ,"shatranj",shatArray, chessIDs},   // Shatranj
  {  8,  8, 3, V_MAKRUK,  "makruk",  thaiArray, chessIDs},   // Makruk
  {  8,  8, 1, V_LION,    "lion",    lionArray, chessIDs},   // Mighty Lion
  { 11, 11, 3, V_WA,      "wa-shogi", waArray,  waIDs},      // Wa
  {  8,  8, 1, V_WOLF,    "werewolf",wolfArray, chessIDs},   // Werewolf Chess
  { 13, 13,13, V_CASHEW,  "cashew-shogi",    cashewArray, dadaIDs },  // Cashew
  { 13, 13,13, V_MACAD,   "macadamia-shogi", macadArray,  makaIDs },  // Macadamia

  { 0, 0, 0, 0 }, // sentinel
  { 17, 17, 0, V_DADA,    "dada",    chuArray }, // Dai Dai
  { 19, 19, 0, V_MAKA,    "maka",    chuArray }, // Maka Dai Dai
  { 25, 25, 0, V_TAI,     "tai",     chuArray }, // Tai
  { 36, 36, 0, V_KYOKU,   "kyoku",   chuArray }  // Taikyoku
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
// Promotion zones
//   the promoBoard contains one byte with flags for each square, to indicate for each side whether the square
//   is in the promotion zone (twice), on the last two ranks, or on the last rank
//   the promoFlag field in the piece list can select which bits of this are tested, to see if it
//   (1) can promote (2) can defer when the to-square is on last rank, last two ranks, or anywhere.
//   Pawns normally can't defer anywhere, but if the user defers with them, their promoFlag is set to promote on last rank only

typedef struct {
  int pos;
  int pieceKey;
  int promo; // ???
  int value;
  int pst;
  signed char range[RAYS];
  char promoFlag;
  char qval;
  char mobility;
  char mobWeight;
  unsigned char promoGain;
  char bulk;
  char ranking;
} PieceInfo; // piece-list entry

int pieces[COLORS], royal[COLORS], kylin[COLORS];
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
unsigned char fireBoard[BSIZE]; // flags to indicate squares controlled by Fire Demons
signed char PST[PSTSIZE] = { 0 };

// Maximum of (ranks, files) of ray between squares
#define dist(s1, s2) MAX(abs((s1-s2)/BW), abs((s1-s2)%BW))
