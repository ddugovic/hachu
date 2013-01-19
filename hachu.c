/***********************************************************************/
/*                               HaChu                                 */
/* A WinBoard engine for large (dropless) Shogi variants by H.G.Muller */
/* The engine is based on incremental updating of an attack map and    */
/* mobility scores, since the effort in this only grows proportional   */
/* to board edge length, rather than board area.                       */
/***********************************************************************/

// TODO:
// in GenCapts we do not generate jumps of more than two squares yet
// promotions by pieces with Lion power stepping in & out the zone in same turn
// promotion on capture

#define VERSION "0.1beta"

#define PATH level==0 || path[0] == 0xc4028 &&  (level==1 /*|| path[1] == 0x75967 && (level == 2 || path[2] == 0x3400b && (level == 3))*/)
//define PATH 0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#ifdef WIN32 
#    include <windows.h>
#else
#    include <sys/time.h>
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

#define ONE (currentVariant == V_SHO || currentVariant == V_CHESS)

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

typedef unsigned int Move;

char *MoveToText(Move move, int m);     // from WB driver
void pmap(int *m, int col);
void pboard(int *b);
void pbytes(unsigned char *b);

typedef struct {
  char *name, *promoted;
  int value;
  signed char range[8];
  char ranking;
  int whiteKey, blackKey;
} PieceDesc;

typedef struct {
  int from, to, piece, victim, new, booty, epSquare, epVictim[8], ep2Square, revMoveCount, savKeyL, savKeyH;
  char fireMask;
} UndoInfo;

char *array, fenArray[4000], *reason;
int bWidth, bHeight, bsize, zone, currentVariant;
int stm, xstm, hashKeyH, hashKeyL, framePtr, msp, nonCapts, rootEval, retMSP, retFirst, retDep, pvPtr, level, cnt50, chuFlag=1, tenFlag, mobilityScore;
int nodes, startTime, tlim1, tlim2, repCnt, comp;
Move retMove, moveStack[10000], path[100], repStack[300], pv[1000], repeatMove[300];

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

#define LVAL 100 /* piece value of Lion. Used in chu for recognizing it to implement Lion-trade rules  */
#define FVAL 500 /* piece value of Fire Demon. Used in code for recognizing moves with it and do burns */

PieceDesc chuPieces[] = {
  {"LN", "", LVAL, { L,L,L,L,L,L,L,L } }, // lion
  {"FK", "",   60, { X,X,X,X,X,X,X,X } }, // free king
  {"SE", "",   55, { X,D,X,X,X,X,X,D } }, // soaring eagle
  {"HF", "",   50, { D,X,X,X,X,X,X,X } }, // horned falcon
  {"FO", "",   40, { X,X,0,X,X,X,0,X } }, // flying ox
  {"FB", "",   40, { 0,X,X,X,0,X,X,X } }, // free boar
  {"DK", "SE", 40, { X,1,X,1,X,1,X,1 } }, // dragon king
  {"DH", "HF", 35, { 1,X,1,X,1,X,1,X } }, // dragon horse
  {"WH", "",   35, { X,X,0,0,X,0,0,X } }, // white horse
  {"R",  "DK", 30, { X,0,X,0,X,0,X,0 } }, // rook
  {"FS", "",   30, { X,1,1,1,X,1,1,1 } }, // flying stag
  {"WL", "",   25, { X,0,0,X,X,X,0,0 } }, // whale
  {"K",  "",   28, { 1,1,1,1,1,1,1,1 }, 4 }, // king
  {"CP", "",   27, { 1,1,1,1,1,1,1,1 }, 4 }, // king
  {"B",  "DH", 25, { 0,X,0,X,0,X,0,X } }, // bishop
  {"VM", "FO", 20, { X,0,1,0,X,0,1,0 } }, // vertical mover
  {"SM", "FB", 20, { 1,0,X,0,1,0,X,0 } }, // side mover
  {"DE", "CP", 20, { 1,1,1,1,0,1,1,1 } }, // drunk elephant
  {"BT", "FS", 15, { 0,1,1,1,1,1,1,1 } }, // blind tiger
  {"G",  "R",  15, { 1,1,1,0,1,0,1,1 } }, // gold
  {"FL", "B",  15, { 1,1,0,1,1,1,0,1 } }, // ferocious leopard
  {"KN", "LN", 15, { J,1,J,1,J,1,J,1 } }, // kirin
  {"PH", "FK", 15, { 1,J,1,J,1,J,1,J } }, // phoenix
  {"RV", "WL", 15, { X,0,0,0,X,0,0,0 } }, // reverse chariot
  {"L",  "WH", 15, { X,0,0,0,0,0,0,0 } }, // lance
  {"S",  "VM", 10, { 1,1,0,1,0,1,0,1 } }, // silver
  {"C",  "SM", 10, { 1,1,0,0,1,0,0,1 } }, // copper
  {"GB", "DE", 5,  { 1,0,0,0,1,0,0,0 } }, // go between
  {"P",  "G",  4,  { 1,0,0,0,0,0,0,0 } }, // pawn
  { NULL }  // sentinel
};

PieceDesc shoPieces[] = {
  {"DK", "",   70, { X,1,X,1,X,1,X,1 } }, // dragon king
  {"DH", "",   52, { 1,X,1,X,1,X,1,X } }, // dragon horse
  {"R",  "DK", 50, { X,0,X,0,X,0,X,0 } }, // rook
  {"B",  "DH", 32, { 0,X,0,X,0,X,0,X } }, // bishop
  {"K",  "",   41, { 1,1,1,1,1,1,1,1 } }, // king
  {"CP", "",   40, { 1,1,1,1,1,1,1,1 } }, // king
  {"DE", "CP", 25, { 1,1,1,1,0,1,1,1 } }, // silver
  {"G",  "",   22, { 1,1,1,0,1,0,1,1 } }, // gold
  {"S",  "G",  20, { 1,1,0,1,0,1,0,1 } }, // silver
  {"L",  "G",  15, { X,0,0,0,0,0,0,0 } }, // lance
  {"N",  "G",  11, { N,0,0,0,0,0,0,N } }, // Knight
  {"P",  "G",  8,  { 1,0,0,0,0,0,0,0 } }, // pawn
  { NULL }  // sentinel
};

PieceDesc daiPieces[] = {
  {"FD", "G", 15, { 0,2,0,2,0,2,0,2 } }, // Flying Dragon
  {"VO", "G", 20, { 2,0,2,0,2,0,2,0 } }, // Violent Ox
  {"EW", "G",  8, { 1,1,1,0,0,0,1,1 } }, // Evil Wolf
  {"CS", "G",  7, { 0,1,0,1,0,1,0,1 } }, // Cat Sword
  {"AB", "G",  6, { 1,0,1,0,1,0,1,0 } }, // Angry Boar
  {"I",  "G",  8, { 1,1,0,0,0,0,0,1 } }, // Iron
  {"N",  "G",  6, { N,0,0,0,0,0,0,N } }, // Knight
  {"ST", "G",  5, { 0,1,0,0,0,0,0,1 } }, // Stone
  { NULL }  // sentinel
};

PieceDesc ddPieces[] = {
  {"LO", "",   1, { 1,H,1,H,1,H,1,H } }, // Long-Nosed Goblin
  {"OK", "LO", 1, { 2,1,2,0,2,0,2,1 } }, // Old Kite
  {"PS", "HM", 1, { J,0,1,J,0,J,1,0 } }, // Poisonous Snake
  {"GE", "",   1, { 3,3,5,5,3,5,5,3 } }, // Great Elephant
  {"WS", "LD", 1, { 1,1,2,0,1,0,2,1 } }, // Western Barbarian
  {"EA", "LN", 1, { 2,1,1,0,2,0,1,1 } }, // Eastern Barbarian
  {"NO", "FE", 1, { 0,2,1,1,0,1,1,2 } }, // Northern Barbarian
  {"SO", "WE", 1, { 0,1,1,2,0,2,1,1 } }, // Southern Barbarian
  {"FE", "",   1, { 2,X,2,2,2,2,2,X } }, // Fragrant Elephant
  {"WE", "",   1, { 2,2,2,X,2,X,2,2 } }, // White Elephant
  {"FT", "",   1, { X,X,5,0,X,0,5,X } }, // Free Dream-Eater
  {"FR", "",   1, { 5,X,X,0,5,0,X,X } }, // Free Demon
  {"WB", "FT", 1, { 2,X,X,X,2,X,X,X } }, // Water Buffalo
  {"RB", "FR", 1, { X,X,X,X,0,X,X,X } }, // Rushing Bird
  {"SB", "",   1, { X,X,2,2,2,2,2,X } }, // Standard Bearer
  {"FH", "FK", 1, { 1,2,1,0,1,0,1,2 } }, // Flying Horse
  {"NK", "SB", 1, { 1,1,1,1,1,1,1,1 } }, // Neighbor King
  {"BM", "MW", 1, { 0,1,1,1,0,1,1,1 } }, // Blind Monkey
  {"DO", "",   1, { 2,X,2,X,2,X,2,X } }, // Dove
  {"EB", "DO", 1, { 2,0,2,0,0,0,2,0 } }, // Enchanted Badger
  {"EF", "SD", 1, { 0,2,0,0,2,0,0,2 } }, // Enchanted Fox
  {"RA", "",   1, { X,0,X,1,X,1,X,0 } }, // Racing Chariot
  {"SQ", "",   1, { X,1,X,0,X,0,X,1 } }, // Square Mover
  {"PR", "SQ", 1, { 1,1,2,1,0,1,2,1 } }, // Prancing Stag
  {"WT", "",   1, { X,1,2,0,X,0,2,X } }, // White Tiger
  {"BD", "",   1, { 2,X,X,0,2,0,X,1 } }, // Blue Dragon
  {"HD", "",   1, { X,0,0,0,1,0,0,0 } }, // Howling Dog
  {"VB", "",   1, { 0,2,1,0,0,0,1,2 } }, // Violent Bear
  {"SA", "",   1, { 2,1,0,0,2,0,0,1 } }, // Savage Tiger
  {"W",  "",   1, { 0,2,0,0,0,0,0,2 } }, // Wood
  {"CS", "DH",  7, { 0,1,0,1,0,1,0,1 } }, // cat sword
  {"FD", "DK", 15, { 0,2,0,2,0,2,0,2 } }, // flying dragon
  {"KN", "GD", 15, { J,1,J,1,J,1,J,1 } }, // kirin
  {"PH", "GB", 15, { 1,J,1,J,1,J,1,J } }, // phoenix
  {"LN", "FF",  100, { L,L,L,L,L,L,L,L } }, // lion
  {"LD", "GE", 1, { T,T,T,T,T,T,T,T } }, // Lion Dog
  {"AB", "", 1, { 1,0,1,0,1,0,1,0 } }, // Angry Boar
  {"B",  "", 1, { 0,X,0,X,0,X,0,X } }, // Bishop
  {"C",  "", 1, { 1,1,0,0,1,0,0,1 } }, // Copper
  {"DH", "", 1, { 1,X,1,X,1,X,1,X } }, // Dragon Horse
  {"DK", "", 1, { X,1,X,1,X,1,X,1 } }, // Dragon King
  {"FK", "", 1, {  } }, // 
  {"EW", "", 1, { 1,1,1,0,0,0,1,1 } }, // Evil Wolf
  {"FL", "", 1, {  } }, // 
  {"", "", 1, {  } }, // 
  {"", "", 1, {  } }, // 
  {"", "", 1, {  } }, // 
  {"", "", 1, {  } }, // 
  { NULL }  // sentinel
};

PieceDesc makaPieces[] = {
  {"DV", "", 1, { 0,1,0,1,0,0,1,1 } }, // Deva
  {"DS", "", 1, { 0,1,1,0,0,1,0,1 } }, // Dark Spirit
  {"T",  "", 1, { 0,1,0,0,1,0,0,1 } }, // Tile
  {"CS", "", 1, { 1,0,0,1,1,1,0,0 } }, // Coiled Serpent
  {"RD", "", 1, { 1,0,1,1,1,1,1,0 } }, // Reclining Dragon
  {"CC", "", 1, { 0,1,1,0,1,0,1,1 } }, // Chinese Cock
  {"OM", "", 1, { 0,1,0,1,1,1,0,1 } }, // Old Monkey
  {"BB", "", 1, { 0,1,0,1,X,1,0,1 } }, // Blind Bear
  {"OR", "", 1, { 0,2,0,0,2,0,0,2 } }, // Old Rat
  {"LD", "WS", 1, { T,T,T,T,T,T,T,T } }, // Lion Dog
  {"WR", "", 1, { 0,3,1,3,0,3,1,3 } }, // Wrestler
  {"GG", "", 1, { 3,1,3,0,3,0,3,1 } }, // Guardian of the Gods
  {"BD", "", 1, { 0,3,1,0,1,0,1,3 } }, // Budhist Devil
  {"SD", "", 1, { 5,2,5,2,5,2,5,2 } }, // She-Devil
  {"DY", "", 1, { J,0,1,0,J,0,1,0 } }, // Donkey
  {"CP", "", 1, { 0,H,0,2,0,2,0,H } }, // Capricorn
  {"HM", "", 1, { H,0,H,0,H,0,H,0 } }, // Hook Mover
  {"SF", "", 1, { 0,1,X,1,0,1,0,1 } }, // Side Flier
  {"LC", "", 1, { X,0,0,X,1,0,0,X } }, // Left Chariot
  {"RC", "", 1, { X,X,0,0,1,X,0,0 } }, // Right Chariot
  {"FG", "", 1, { X,X,X,0,X,0,X,X } }, // Free Gold
  {"FS", "", 1, { X,X,0,X,0,X,0,X } }, // Free Silver
  {"FC", "", 1, { X,X,0,0,X,0,0,X } }, // Free Copper
  {"FI", "", 1, { X,X,0,0,0,0,0,X } }, // Free Iron
  {"FT", "", 1, { 0,X,0,0,X,0,0,X } }, // Free Tile
  {"FN", "", 1, { 0,X,0,0,0,0,0,X } }, // Free Stone
  {"FTg", "", 1, { 0,X,X,X,X,X,X,X } }, // Free Tiger
  {"FLp", "", 1, { X,X,0,X,X,X,0,X } }, // Free Leopard (Free Boar?)
  {"FSp", "", 1, { X,0,0,X,X,X,0,0 } }, // Free Serpent (Whale?)
  {"FrD", "", 1, { X,0,X,X,X,X,X,0 } }, // Free Dragon
  {"FC", "", 1, { 0,X,0,X,0,X,0,X } }, // Free Cat (Bishop?)
  {"EM", "", 1, {  } }, // Emperor
  {"TK", "", 1, {  } }, // Teaching King
  {"BS", "", 1, {  } }, // Budhist Spirit
  {"WS", "", 1, { X,X,0,X,1,X,0,X } }, // Wizard Stork
  {"MW", "", 1, { 1,X,0,X,X,X,0,X } }, // Mountain Witch
  {"FF", "", 1, {  } }, // Furious Fiend
  {"GD", "", 1, { 2,3,X,3,2,3,X,3 } }, // Great Dragon
  {"GB", "", 1, { X,3,2,3,X,3,2,3 } }, // Golden Bird
  {"FrW", "", 1, {  } }, // Free Wolf
  {"FrB", "", 1, {  } }, // Free Bear
  {"BT", "", 1, { X,0,0,X,0,X,0,0 } }, // Bat
  {"", "", 1, {  } }, // 
  { NULL }  // sentinel
};

PieceDesc taiPieces[] = {
  {"", "", 1, {  } }, // Peacock
  {"", "", 1, {  } }, // Vermillion Sparrow
  {"", "", 1, {  } }, // Turtle Snake
  {"", "", 1, {  } }, // Silver Hare
  {"", "", 1, {  } }, // Golden Deer
  {"", "", 1, {  } }, // 
  {"", "", 1, {  } }, // 
  {"", "", 1, {  } }, // 
  { NULL }  // sentinel
};

PieceDesc tenjikuPieces[] = { // only those not in Chu, or different (because of different promotion)
  {"FI", "", FVAL, { X,X,0,X,X,X,0,X } }, // Fire Demon
  {"GG", "", 150, { R,R,R,R,R,R,R,R }, 3 }, // Great General
  {"VG", "", 140, { 0,R,0,R,0,R,0,R }, 2 }, // Vice General
  {"RG", "GG",120, { R,0,R,0,R,0,R,0 }, 1 }, // Rook General
  {"BG", "VG",110, { 0,R,0,R,0,R,0,R }, 1 }, // Bishop General
  {"SE", "RG", 1, { X,D,X,X,X,X,X,D } }, // Soaring Eagle
  {"HF", "BG", 1, { D,X,X,X,X,X,X,X } }, // Horned Falcon
  {"LH", "",   1, { L,S,L,S,L,S,L,S } }, // Lion-Hawk
  {"LN", "LH", LVAL, { L,L,L,L,L,L,L,L } }, // Lion
  {"FE", "",   1,   { X,X,X,X,X,X,X,X } }, // Free Eagle
  {"FK", "FE",  60, { X,X,X,X,X,X,X,X } }, // Free King
  {"HT", "",   1, { X,X,2,X,X,X,2,X } }, // Heavenly Tetrarchs
  {"CS", "HT", 1, { X,X,2,X,X,X,2,X } }, // Chariot Soldier
  {"WB", "FI", 1, { 2,X,X,X,2,X,X,X } }, // Water Buffalo
  {"VS", "CS", 1, { X,0,2,0,1,0,2,0 } }, // Vertical Soldier
  {"SS", "WB", 1, { 2,0,X,0,1,0,X,0 } }, // Side Soldier
  {"I",  "VS", 1, { 1,1,0,0,0,0,0,1 } }, // Iron
  {"N",  "SS", 1, { N,0,0,0,0,0,0,N } }, // Knight
  {"MG", "",   1, { X,0,0,X,0,X,0,0 } }, // Multi-General
  {"D",  "MG", 1, { 1,0,0,1,0,1,0,0 } }, // Dog
  { NULL }  // sentinel
};

PieceDesc taikyokuPieces[] = {
  {"", "", 1, {  } }, // 
  { NULL }  // sentinel
};

PieceDesc chessPieces[] = {
  {"Q", "",  95, { X,X,X,X,X,X,X,X } },
  {"R", "",  50, { X,0,X,0,X,0,X,0 } },
  {"B", "",  32, { 0,X,0,X,0,X,0,X } },
  {"N", "",  30, { N,N,N,N,N,N,N,N } },
  {"K", "",  28, { 1,1,1,1,1,1,1,1 } },
  {"P", "Q",  8, { M,C,0,0,0,0,0,C } },
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
char chessArray[] = "RNBQKBNR/PPPPPPPP/......../......../......../......../pppppppp/rnbqkbnr";

typedef struct {
  int boardWidth, boardFiles, boardRanks, zoneDepth, varNr; // board sizes
  char *name;  // WinBoard name
  char *array; // initial position
} VariantDesc;

typedef enum { V_CHESS, V_SHO, V_CHU, V_DAI, V_TENJIKU, V_DADA, V_MAKA, V_TAI, V_KYOKU } Variant;

VariantDesc variants[] = {
  { 16,  8,  8, 1, V_CHESS,  "normal", chessArray }, // FIDE
  { 18,  9,  9, 3, V_SHO, "9x9+0_shogi", shoArray }, // Sho
  { 24, 12, 12, 4, V_CHU,     "chu",     chuArray }, // Chu
  { 30, 15, 15, 5, V_DAI,     "dai",     daiArray }, // Dai
  { 32, 16, 16, 5, V_TENJIKU, "tenjiku", tenArray }, // Tenjiku
//  { 0, 0, 0, 0, 0 }, // sentinel
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

int epList[96], ep2List[96], toList[96], reverse[96];  // decoding tables for double and triple moves
int kingStep[10], knightStep[10];         // raw tables for step vectors (indexed as -1 .. 8)
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
} PieceInfo; // piece-list entry

int last[2], royal[2];
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
    int resign;         // engine-defined option
    int contemptFactor; // likewise

int squareKey[BSIZE];

int rawBoard[BSIZE + 11*BHMAX + 6];
//int attacks[2*BSIZE];   // attack map
int attackMaps[200*BSIZE], *attacks = attackMaps;
char distance[2*BSIZE]; // distance table
char promoBoard[BSIZE]; // flags to indicate promotion zones
char rawFire[BSIZE+2*BWMAX]; // flags to indicate squares controlled by Fire Demons
signed char PST[2*BSIZE];

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
  }
  return NULL;
}

void
SqueezeOut (int n)
{ // remove piece number n from the mentioned side's piece list (and adapt the reference to the displaced pieces!)
  int i;
  for(i=stm+2; i<last[stm]; i+=2)
    if(p[i].promo > n) p[i].promo -= 2;
  for(i=n; i<last[stm]; i+=2) {
    p[i] = p[i+2];
    if(i+2 == royal[stm]) royal[stm] -= 2; // NB: squeezing out the King moves up Crown Prince to royal[stm]
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
    if(r[i] < 0) d == r[i] >= L ? 2 : 36;
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
  for(i=col+2; i<last[col]; i+=2) { // scan piece list for multi-capturers
    for(j=0; j<8; j++) if(p[i].range[j] < J && p[i].range[j] >= S || p[i].value == 10*FVAL) {
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
  for(i=stm+2; i<last[stm]; i+=2) { // first pass: unpromoted pieces
    if((k = p[i].promo) >= 0 && p[i].pos == ABSENT) { // unpromoted piece no longer there
      p[k].promo = -2; // orphan promoted version
      SqueezeOut(i);
    }
  }
  for(i=stm+2; i<last[stm]; i+=2) { // second pass: promoted pieces
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
    if(p[i].value < 10*list->value || p[i].value == 10*list->value && (p[i].promo < 0)) break;
  }
  last[stm] += 2;
  for(j=last[stm]; j>i; j-= 2) p[j] = p[j-2];
  p[i].value = v = 10*list->value;
  for(j=0; j<8; j++) p[i].range[j] = list->range[j^4*(WHITE-stm)];
  switch(Range(p[i].range)) {
    case 1:  p[i].pst = BH; break;
    case 2:  p[i].pst = bsize; break;
    default: p[i].pst = bsize + BH; break;
  }
  key = (stm == WHITE ? &list->whiteKey : &list->blackKey);
  if(!*key) *key = ~(myRandom()*myRandom());
  p[i].pieceKey = *key;
  p[i].promoFlag = 0;
  p[i].mobWeight = v > 600 ? 0 : v >= 400 ? 1 : v >= 300 ? 2 : v > 150 ? 3 : v >= 100 ? 2 : 0;
  for(j=stm+2; j<= last[stm]; j+=2) {
    if(p[j].promo >= i) p[j].promo += 2;
  }
  if(royal[stm] >= i) royal[stm] += 2;
  if(p[i].value == (currentVariant == V_SHO ? 410 : 280) ) royal[stm] = i;
  p[i].qval = (currentVariant == V_TENJIKU ? list->ranking : 0); // jump-capture hierarchy
  return i;
}

void
SetUp(char *array, int var)
{
  int i, j, n, m, nr, color;
  char c, *q, name[3];
  PieceDesc *p1, *p2;
  last[WHITE] = 1; last[BLACK] = 0;
  if(var == V_CHESS) // add dummy Crown Princes
    p[AddPiece(WHITE, LookUp("CP", V_CHU))].pos =  p[AddPiece(BLACK, LookUp("CP", V_CHU))].pos = ABSENT;
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
      p1 = LookUp(name, var);
      if(!p1) printf("tellusererror Unknown piece '%s' in setup\n", name), exit(-1);
      if(pflag && p1->promoted) p1 = LookUp(p1->promoted, var); // use promoted piece instead
      n = AddPiece(color, p1);
      p[n].pos = j;
      if(p1->promoted[0] && !pflag) {
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
      } else p[n].promo = -1; // unpromotable piece
//printf("piece = %c%-2s %d(%d) %d/%d\n", color ? 'w' : 'b', name, n, m, last[color], last[!color]);
    }
  }
 eos:
  for(i=0; i<8; i++)  fireFlags[i] = 0;
  for(i=2, n=1; i<10; i++) if(p[i].value == 10*FVAL) {
    int x = p[i].pos; // mark all burn zones
    fireFlags[i-2] = n;
    if(x != ABSENT) for(j=0; j<8; j++) fireBoard[x+kStep[j]] |= n;
    n <<= 1;
  }
  for(i=0; i<BH; i++) for(j=0; j<BH; j++) board[BW*i+j] = EMPTY;
  for(i=WHITE+2; i<=last[WHITE]; i+=2) board[p[i].pos] = i;
  for(i=BLACK+2; i<=last[BLACK]; i+=2) board[p[i].pos] = i;
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

  currentVariant = variants[var].varNr;
  bWidth  = variants[var].boardWidth;
  bHeight = variants[var].boardRanks;
  zone    = variants[var].zoneDepth;
  array   = variants[var].array;
  bsize = bWidth*bHeight;
  chuFlag = (currentVariant == V_CHU);
  tenFlag = (currentVariant == V_TENJIKU);

  for(i= -1; i<9; i++) { // board steps in linear coordinates
    kStep[i] = STEP(direction[i&7].x,   direction[i&7].y);       // King
    nStep[i] = STEP(direction[(i&7)+8].x, direction[(i&7)+8].y); // Knight
  }

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

  // fill distance table
  for(i=0; i<2*BSIZE; i++) {
    distance[i] = 0;
  }
  for(i=0; i<8; i++)
    for(j=1; j<BH; j++)
      dist[j * kStep[i]] = j;
  if(currentVariant == V_TENJIKU)
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
  for(i=0; i<BH; i++) for(j=0; j<BH; j++) {
    int s = BW*i + j, d = BH*(BH-2) - abs(2*i - BH + 1)*(BH-1) - (2*j - BH + 1)*(2*j - BH + 1);
    PST[s] = 0;
    PST[BH+s] = d/4 - (i == 0 || i == BH-1 ? 15 : 0) - (j == 0 || j == BH-1 ? 15 : 0);
    PST[BH*BW+s] = d/6;
    PST[BH*BW+BH+s] = d/12;
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

int flag;

inline int
NewNonCapture (int x, int y, int promoFlags)
{
  if(board[y] != EMPTY) return 1; // edge, capture or own piece
//if(flag) printf("# add %c%d%c%d, pf=%d\n", x%BW+'a',x/BW,y%BW+'a',y/BW, promoFlags);
  if( (promoBoard[x] | promoBoard[y]) & promoFlags) { // piece can promote with this move
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
	  if(!NewNonCapture(x, x+v, pFlag) && promoBoard[x-v])
	    NewNonCapture(x, x+2*v+DEFER, pFlag); // use promoSuppress flag as e.p. flag
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

void
Connect (int sqr, int piece, int dir)
{ // scan to both sides along ray to elongate attacks from there, and remove our own attacks on there, if needed
  int x, step = kStep[dir], r1 = p[piece].range[dir], r2 = p[piece].range[dir+4], r3, r4, piece1, piece2;
  int d1, d2, r, y, c;

  if((attacks[2*sqr] + attacks[2*sqr+1]) & attackMask[dir]) {         // there are incoming attack(s) from 'behind'
    x = sqr;
    while(board[x-=step] == EMPTY);                                   // in any case, scan to attacker, to see where / what it is
    d1 = dist[x-sqr]; piece1 = board[x];
    attacks[2*x + stm] -= -(d1 <= r2) & one[dir+4];                   // remove our attack on it if in-range
    if((attacks[2*sqr] + attacks[2*sqr+1]) & attackMask[dir+4]) {     // there are also incoming attack(s) from 'ahead'

      y = sqr;
      while(board[y+=step] == EMPTY);                                 // also always scan to that one to see what it is
      d2 = dist[y-sqr]; piece2 = board[y];
      attacks[2*y+stm] -= -(d2 <= r1) & one[dir];                     // remove our attack on it if in-range
      // we have two pieces now shooting at each other. See how far they get.
      if(d1 + d2 <= (r3 = p[piece1].range[dir])) {                    // 1 hits 2
	attacks[2*y + (piece1 & WHITE)] += one[dir];                  // count attack
	UPDATE_MOBILITY(piece1, d2);
      } else UPDATE_MOBILITY(piece1, r3 - d1);                        // does not connect, but could still gain mobility
      if(d1 + d2 <= (r4 = p[piece2].range[dir+4])) {                  // 2 hits 1
	attacks[2*x + (piece2 & WHITE)] += one[dir+4];                // count attack
	UPDATE_MOBILITY(piece2, d1);
      } else UPDATE_MOBILITY(piece2, r4 - d2);                        // does not connect, but could still gain mobility
      // if r1 or r2<0, moves typically jump, and thus cannot be unblocked. Exceptions are FF and BS distant moves.
      // test for d1+d2 > 2 && rN == F && d== 3 or rN == S
      if(d1 <= 2) { // could be jump interactions
	if(d1 == 2) {
	  if(r2 <= J) attacks[2*x + stm] -= one[dir+4];
	  if(r1 <= J) attacks[2*y + stm] -= one[dir];
	} else { // d1 == 1
	  if(r2 < J) attacks[2*x + stm] -= one[dir+4];
	  if(r1 < J) attacks[2*y + stm] -= one[dir];
	  if(board[x-step] != EMPTY && board[x-step] != EDGE)
	    attacks[2*(x-step) + stm] -= one[dir+4];
	}
      }

    } else { // we were only attacked from behind

      r = (r2 = p[piece1].range[dir]) - d1;
      if(r < 0 || c > one[dir+4]) { // Oops! This was not our attacker, or not the only one. There must be a jump attack from even further behind!
	// for now, forget jumpers
      }
      y = sqr; 
      while(r--)
	if(board[y+=step] != EMPTY) {
	  d2 = dist[y-sqr]; piece2 = board[y];
	  if(piece2 != EDGE) {                                // extended move hits a piece
	    attacks[2*y + (piece1 & WHITE)] += one[dir];      // count attack
	    attacks[2*y + stm] -= -(d2 <= r1) & one[dir];     // remove our own attack on it, if in-range
	  }
	  UPDATE_MOBILITY(piece1, d2);                        // count extra mobility even if we hit edge
	  return;
	}
      // we hit nothing with the extended move of the attacker behind us.
      UPDATE_MOBILITY(piece1, r2 - d1);
      r = r1 - r2 + d1;                                       // extra squares covered by mover
      while(r-- > 0)
	if(board[y+=step] != EMPTY) {
	  d2 = dist[y-sqr]; piece2 = board[y];
	  if(piece2 != EDGE) {                                // extended move hits a piece
	    attacks[2*y + stm] -= one[dir];                   // count attack
	  }
	  return;
	}
    }
    // if r2<0 we should again test for F and S moves

  } else // no incoming attack from behind
  if(c = (attacks[2*sqr] + attacks[2*sqr+1]) & attackMask[dir+4]) { // but incoming attack(s) from 'ahead'

      y = sqr; while(board[y+=step]);                               // locate attacker
      d2 = dist[y-sqr]; piece2 = board[y];
      attacks[2*y + stm] -= -(d2 <= r1) & one[dir];                 // remove our attack on it if in-range
      r = (r1 = p[piece1].range[dir]) - d2;
      if(r < 0 || c > one[dir]) { // Oops! This was not our attacker, or not the only one. There must be a jump attack from even further behind!
	// for now, forget jumpers
      }
      x = sqr;
      while(r--)
	if(board[x-=step] != EMPTY) {
	  d1 = dist[x-sqr]; piece1 = board[x];
	  if(piece1 != EDGE) {                                      // extended move hits a piece
	    attacks[2*x + (piece2 & WHITE)] += one[dir+4];          // count attack
	    attacks[2*x + stm] -= -(d1 <= r2) & one[dir+4];         // remove our own attack on it, if in-range
	  }
	  UPDATE_MOBILITY(piece2, d1);                              // count extra mobility even if we hit edge
	  return;
	}
      // we hit nothing with the extended move of the attacker behind us.
      UPDATE_MOBILITY(piece2, r2 - d1);
      r = r2 - r1 + d2;                                             // extra squares covered by mover
      while(r-- > 0)
	if(board[x-=step] != EMPTY) {
	  d1 = dist[x-sqr]; piece1 = board[x];
	  if(piece1 != EDGE) {                                      // extended move hits a piece
	    attacks[2*x + stm] -= one[dir+4];                       // count attack
	  }
	  return;
	}

  } else { // no incoming attacks from either side. Only delete attacks of mover on others

    x = sqr;
    while(r1--)
      if(board[x+=step] != EMPTY) {       // piece found that we attacked
	attacks[2*x + stm] -= one[dir];   // decrement attacks along that direction
	break;
      }

    x = sqr;
    while(r2--)
      if(board[x-=step] != EMPTY) {       // piece found that we attacked
	attacks[2*x + stm] -= one[dir+4]; // decrement attacks along opposite direction
	break;
      }

  }
}

inline int
Hit (int r, int d)
{ // test if move with range r reaches over (un-obstructed) distance d
  if(r < 0) switch(r) {
    case J: return (d == 2);
    case D:
    case L: return (d <= 2);
    case T:
    case F: return (d <= 3);
    case S: return 1;
    default: return 0;
  } else return (d <= r);
  return 0; // not reached
}

void
Disconnect (int sqr, int piece, int dir)
{
  int x = sqr, step = kStep[dir], piece1, piece2, d1, d2, r1, r2, y;
  while( board[x+=step] == EMPTY );
  piece1 = board[x];
  if(piece1 != EDGE) { // x has hit a piece
    d1 = dist[x-sqr];
    r1 = p[piece1].range[dir+4];
    y = sqr; while( board[y-=step] == EMPTY );
    piece2 = board[y];
    if(piece2 != EDGE) { // both ends of the ray hit a piece
      d2 = dist[y-sqr];
      r2 = p[piece2].range[dir];
      if(r1 >= d1) {      // piece1 hits us
	attacks[2*sqr + (piece1 & WHITE)] += one[dir+4];
	if(r1 >= d1 + d2) // was hitting piece2 before, now blocked
	  attacks[2*y + (piece1 & WHITE)] -= one[dir+4];
      }
      if(r2 >= d2) {      // piece2 hits us
	attacks[2*sqr + (piece2 & WHITE)] += one[dir];
	if(r2 >= d1 + d2) // was hitting piece1 before, now blocked
	  attacks[2*x + (piece2 & WHITE)] -= one[dir];
      }
      if( Hit(p[piece].range[dir], d1) )
	attacks[2*sqr + stm] += one[dir];
      if( Hit(p[piece].range[dir+4], d2) )
	attacks[2*sqr + stm] += one[dir+4];
      return;
    }
  } else {
    x = sqr; while( board[x-=step] == EMPTY );
    piece1 = board[x];
    if(piece1 == EDGE) return; // ray empty on both sides
    d1 = dist[x-sqr];
    r1 = p[piece1].range[dir];
    dir += 4;
  }
  // we only get here if one side looks to the board edge
  if(r1 >= d1) // piece1 hits us
    attacks[2*sqr + (piece1 & WHITE)] += one[dir^4];
  if( Hit(p[piece].range[dir], d1) )
    attacks[2*sqr + stm] += one[dir];
}

void
Occupy (int sqr)
{ // determines attacks on square and blocking when a piece lands on an empty square
  int i;
  for(i=0; i<4; i++) {
    Disconnect(sqr, board[sqr], i);
  }
}

void
Evacuate (int sqr, int piece)
{ // determines change in attacks on neighbors due to unblocking and mover when the mentioned piece vacates the given square
  int i;
  for(i=0; i<4; i++) Connect(sqr, piece, i);
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
  u->revMoveCount = cnt50++;
  u->savKeyL = hashKeyL;
  u->savKeyH = hashKeyH;
  u->epVictim[0] = EMPTY;

  if(p[u->piece].promoFlag & LAST_RANK) cnt50 = 0; // forward piece: move is irreversible
  // TODO: put in some test for forward moves of non-backward pieces?

  if(p[u->piece].value == 10*FVAL) { // move with Fire Demon
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
    hashKeyL ^= p[u->epVictim[0]].pieceKey * squareKey[u->epSquare];
    hashKeyH ^= p[u->epVictim[0]].pieceKey * squareKey[u->epSquare+BH];
    hashKeyL ^= p[u->epVictim[1]].pieceKey * squareKey[u->ep2Square];
    hashKeyH ^= p[u->epVictim[1]].pieceKey * squareKey[u->ep2Square+BH];
    if(p[u->piece].value != 10*LVAL && p[u->epVictim[0]].value == 10*LVAL) deferred |= PROMOTE; // flag non-Lion x Lion
    cnt50 = 0; // double capture irreversible
  }

  if(u->fireMask & fireBoard[u->to]) { // we moved next to enemy Fire Demon (must be done after SPECIAL, to decode to-sqr)
    p[u->piece].pos = ABSENT;          // this is suicide: implement as promotion to EMPTY
    u->new = EMPTY;
    u->booty -= p[u->piece].value;
    cnt50 = 0;
  } else
  if(p[u->piece].value == 10*FVAL) { // move with Fire Demon that survives: burn
    int i, f=fireFlags[u->piece-2];
    for(i=0; i<8; i++) {
	int x = u->to + kStep[i], burnVictim = board[x];
	fireBoard[x] |= f;  // mark new burn zone
	u->epVictim[i+1] = burnVictim; // remember all neighbors, just in case
	if(burnVictim != EMPTY && (burnVictim & TYPE) == xstm) { // opponent => actual burn
	  board[x] = EMPTY; // remove it
	  p[burnVictim].pos = ABSENT;
	  u->booty += p[burnVictim].value + PST[p[burnVictim].pst + x];
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
  u->booty += p[u->victim].value + PST[p[u->victim].pst + u->to];
  if(u->victim != EMPTY) cnt50 = 0; // capture irreversible

  p[u->new].pos = u->to;
  board[u->to] = u->new;

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

  if(p[u->piece].value == 10*FVAL) {
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
Evaluate ()
{
  return (stm ? mobilityScore : -mobilityScore);
}

inline void
FireSet (UndoInfo *tb)
{ // set fireFlags acording to remaining presene of Fire Demons
  int i;
  for(i=stm+2; p[i].value == 10*FVAL; i++) // Fire Demons are always leading pieces in list
    if(p[i].pos != ABSENT) tb->fireMask |= fireFlags[i-2];
}

#define QSdepth 0

int
Search (int alpha, int beta, int difEval, int depth, int oldPromo, int promoSuppress)
{
  int i, j, k, firstMove, oldMSP = msp, curMove, sorted, bad, phase, king, iterDep, replyDep, nextVictim, to, defer, dubious, bestMoveNr;
  int resDep;
  int myPV = pvPtr;
  int score, bestScore, curEval, iterAlpha;
  Move move, nullMove;
  UndoInfo tb;
if(PATH) /*pboard(board),pmap(attacks, BLACK),*/printf("search(%d) {%d,%d} eval=%d, stm=%d\n",depth,alpha,beta,difEval,stm),fflush(stdout);
  xstm = stm ^ WHITE;
//printf("map made\n");fflush(stdout);
  // KING CAPTURE
  k = p[king=royal[xstm]].pos;
  if( k != ABSENT) {
    if(attacks[2*k + stm]) {
      if( p[king + 2].pos == ABSENT ) return INF; // we have an attack on his only King
    }
  } else { // he has no king! Test for attacks on Crown Prince
    k = p[king + 2].pos;
    if(attacks[2*k + stm]) return INF; // we have attack on Crown Prince
  }
//printf("King safe\n");fflush(stdout);
  // EVALUATION & WINDOW SHIFT
  curEval = difEval + Evaluate();
  alpha -= (alpha < curEval);
  beta  -= (beta <= curEval);

  nodes++;
  pv[pvPtr++] = 0; // start empty PV, directly behind PV of parent

  firstMove = curMove = sorted = msp += 50; // leave 50 empty slots in front of move list
  tb.fireMask = phase = 0; iterDep=1; replyDep = (depth < 1 ? depth : 1) - 1;
  do {
if(flag && depth>= 0) printf("iter %d:%d\n", depth,iterDep),fflush(stdout);
    iterAlpha = alpha; bestScore = -INF; bestMoveNr = 0; resDep = 60;
    for(curMove = firstMove; ; curMove++) { // loop over moves
if(flag && depth>= 0) printf("phase=%d: first/curr/last = %d / %d / %d\n", phase, firstMove, curMove, msp);fflush(stdout);
      // MOVE SOURCE
      if(curMove >= msp) { // we ran out of moves; generate some new
	switch(phase) {
	  case 0: // null move
	    if(depth <= 0) {
	      bestScore = curEval; resDep = QSdepth;
	      if(bestScore >= beta || depth < -1) goto cutoff;
	    }
#if 0
	    if(curEval >= beta) {
	      stm ^= WHITE;
	      score = -Search(-beta, -iterAlpha, -difEval, depth-3, promoSuppress & SQUARE, ABSENT);
	      stm ^= WHITE;
	      if(score >= beta) { msp = oldMSP; retDep += 3; return score + (score < curEval); }
	    }
#endif
	    if(tenFlag) FireSet(&tb); // in tenjiku we must identify opposing Fire Demons to perform any moves
//if(PATH) printf("mask=%x\n",tb.fireMask),pbytes(fireBoard);
	    phase = 1;
	  case 1: // hash move
	    phase = 2;
	  case 2: // capture-gen init
	    nextVictim = xstm;
	    phase = 3;
	  case 3: // generate captures
if(PATH) printf("%d:%2d:%2d next victim %d/%d\n",level,depth,iterDep,curMove,msp);
	    while(nextVictim < last[xstm]) {          // more victims exist
	      int group, to = p[nextVictim += 2].pos; // take next
	      if(to == ABSENT) continue;              // ignore if absent
	      if(!attacks[2*to + stm]) continue;      // skip if not attacked
	      group = p[nextVictim].value;            // remember value of this found victim
	      if(iterDep <= QSdepth + 1 && 2*group + curEval + 30 < alpha) { resDep = QSdepth + 1; goto cutoff; }
if(PATH) printf("%d:%2d:%2d group=%d, to=%c%d\n",level,depth,iterDep,group,to%BW+'a',to/BW+ONE);
	      GenCapts(to, 0);
if(PATH) printf("%d:%2d:%2d msp=%d\n",level,depth,iterDep,msp);
	      while(nextVictim < last[xstm] && p[nextVictim+2].value == group) { // more victims of same value exist
		to = p[nextVictim += 2].pos;          // take next
		if(to == ABSENT) continue;            // ignore if absent
		if(!attacks[2*to + stm]) continue;    // skip if not attacked
if(PATH) printf("%d:%2d:%2d p=%d, to=%c%d\n",level,depth,iterDep,nextVictim,to%BW+'a',to/BW+ONE);
		GenCapts(to, 0);
if(PATH) printf("%d:%2d:%2d msp=%d\n",level,depth,iterDep,msp);
	      }
//printf("captures on %d generated, msp=%d\n", nextVictim, msp);
	      goto extractMove;
	    }
//	    if(currentVariant == V_CHESS && promoSuppress != ABSENT) { // e.p.
//		int n = board[promoSuppress-1];
//		if( n != EMPTY && (n&TYPE) == xstm && p[n].value == 8 ) 
//	    }
	    phase = 4; // out of victims: all captures generated
	  case 4: // dubious captures
#if 0
	    while( dubious < framePtr + 250 ) // add dubious captures back to move stack
	      moveStack[msp++] = moveStack[dubious++];
	    if(curMove != msp) break;
#endif
	    phase = 5;
	  case 5: // killers
	    if(depth <= QSdepth) { resDep = QSdepth; goto cutoff; }
	    phase = 6;
	  case 6: // non-captures
	    nonCapts = msp;
	    nullMove = GenNonCapts(oldPromo);
	    phase = 7;
	    sorted = msp; // do not sort noncapts
	    break;
	  case 7: // bad captures
	  case 8: // PV null move
	    phase = 9;
	    if(nullMove != ABSENT) {
	      moveStack[msp++] = nullMove + (nullMove << SQLEN) | DEFER; // kludge: setting DEFER guarantees != 0, and has no effect
	    }
//printf("# %d. sqr = %08x null = %08x\n", msp, nullMove, moveStack[msp-1]);
	  case 9:
	    goto cutoff;
	}
      }

      // MOVE EXTRACTION
    extractMove:
if(flag & depth >= 0) printf("%2d:%d extract %d/%d\n", depth, iterDep, curMove, msp);
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

if(flag & depth >= 0) printf("%2d:%d made %d/%d %s\n", depth, iterDep, curMove, msp, MoveToText(moveStack[curMove], 0));
      for(i=2; i<=cnt50; i+=2) if(repStack[level-i+200] == hashKeyH) {
	moveStack[curMove] = 0; // erase forbidden move
	if(!level) repeatMove[repCnt++] = move & 0xFFFFFF; // remember outlawed move
	score = -INF; moveStack[curMove] = 0; goto repetition;
      }
      repStack[level+200] = hashKeyH;

if(PATH) printf("%d:%2d:%d %3d %6x %-10s %6d %6d\n", level, depth, iterDep, curMove, moveStack[curMove], MoveToText(moveStack[curMove], 0), score, bestScore);
path[level++] = move;
attacks += 2*bsize;
MapFromScratch(attacks); // for as long as incremental update does not work.
//if(flag & depth >= 0) printf("%2d:%d mapped %d/%d %s\n", depth, iterDep, curMove, msp, MoveToText(moveStack[curMove], 0));
//if(PATH) pmap(attacks, stm);
      if(chuFlag && p[tb.victim].value == 10*LVAL) {// verify legality of Lion capture in Chu Shogi
	score = 0;
	if(p[tb.piece].value == 10*LVAL) {          // Ln x Ln: can make Ln 'vulnerable' (if distant and not through intemediate > GB)
	  if(dist[tb.from-tb.to] != 1 && attacks[2*tb.to + stm] && p[tb.epVictim[0]].value <= 50)
	    score = -INF;                           // our Lion is indeed made vulnerable and can be recaptured
	} else {                                    // other x Ln
	  if(promoSuppress & PROMOTE) score = -INF; // non-Lion captures Lion after opponent did same
	  defer |= PROMOTE;                         // if we started, flag  he cannot do it in reply
	}
        if(score == -INF) {
          if(level == 1) repeatMove[repCnt++] = move & 0xFFFFFF | (p[tb.piece].value == 10*LVAL ? 3<<24 : 1 << 24);
          moveStack[curMove] = 0; // zap illegal moves
          goto abortMove;
        }
      }
#if 1
      score = -Search(-beta, -iterAlpha, -difEval - tb.booty, replyDep, promoSuppress & ~PROMOTE, defer);
#else
      score = 0;
#endif
    abortMove:
attacks -= 2*bsize;
level--;
    repetition:
      UnMake(&tb);
      xstm = stm; stm ^= WHITE;
#if 1
if(PATH) printf("%d:%2d:%d %3d %6x %-10s %6d %6d\n", level, depth, iterDep, curMove, moveStack[curMove], MoveToText(moveStack[curMove], 0), score, bestScore);

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
	    // update killer
	    resDep = retDep;
	    goto cutoff;
	  }
	  { int i=pvPtr;
	    for(pvPtr = myPV+1; pv[pvPtr++] = pv[i++]; ); // copy daughter PV
	    pv[myPV] = move;                              // behind our move (pvPtr left at end of copy)
	  }
	}

      }
      if(retDep < resDep) resDep = retDep;
#endif
    } // next move
  cutoff:
    if(!level) { // root node
      if(postThinking > 0) {
        int i;   // WB thinking output
	printf("%d %d %d %d", iterDep, bestScore, (GetTickCount() - startTime)/10, nodes);
	for(i=0; pv[i]; i++) printf(" %s", MoveToText(pv[i], 0));
        if(iterDep == 1) printf(" { root eval = %4.2f dif = %4.2f; abs = %4.2f}", curEval/100., difEval/100., PSTest()/100.);
	printf("\n");
        fflush(stdout);
      }
      if(GetTickCount() - startTime > tlim1) break; // do not start iteration we can (most likely) not finish
    }
    replyDep = iterDep;
  } while(++iterDep <= depth); // next depth
  retMSP = msp;
  retFirst = firstMove;
  msp = oldMSP; // pop move list
  pvPtr = myPV; // pop PV
  retMove = bestMoveNr ? moveStack[bestMoveNr] : 0;
  retDep = resDep + 1;
if(PATH) printf("return %d: %d %d\n", depth, bestScore, curEval);
  return bestScore + (bestScore < curEval);
}

void
pplist()
{
  int i, j;
  for(i=0; i<182; i++) {
	printf("%3d. %3d %3d %4d   %02x %d  ", i, p[i].value, p[i].promo, p[i].pos, p[i].promoFlag&255, p[i].qval);
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
    #define MAXPLY   20   /* maximum search depth */

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
    int  SearchBestMove(int stm, int timeLeft, int mps, int timeControl, int inc, int timePerMove, MOVE *move, MOVE *ponderMove);
    void PonderUntilInput(int stm);         // Search current position for stm, deepening forever until there is input.

UndoInfo undoInfo;
int sup0, sup1, sup2; // promo suppression squares
int lastLift, lastPut;

int
MakeMove2 (int stm, MOVE move)
{
  int i;
  FireSet(&undoInfo);
  sup0 = sup1; sup1 = sup2;
  sup2 = MakeMove(move, &undoInfo);
  if(chuFlag && p[undoInfo.victim].value == 10*LVAL && p[undoInfo.piece].value != 10*LVAL) sup2 |= PROMOTE;
  rootEval = -rootEval - undoInfo.booty;
  for(i=0; i<200; i++) repStack[i] = repStack[i+1];
  repStack[199] = hashKeyH;
printf("# makemove %08x %c%d %c%d\n", move, sup1%BW+'a', sup1/BW, sup2%BW+'a', sup2/BW);
  return stm ^ WHITE;
}

void
UnMake2 (MOVE move)
{
  int i;
  rootEval = -rootEval - undoInfo.booty;
  UnMake(&undoInfo);
  for(i=200; i>0; i--) repStack[i] = repStack[i-1];
  sup2 = sup1; sup1 = sup0;
}

char fenNames[] = "RV....DKDEFL..DHGB......SMLNKN..FK....BT..VM..PH...."; // pairs of char
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
    if(isalpha(*fen)) {
      char *table = fenNames;
      n = *fen > 'Z' ? 'a' - 'A' : 0;
      if(currentVariant == V_CHESS && *fen - 'A' - n == 'N' // In Chess N is Knight, not Lion
           || table[2* (*fen - 'A' - n)] == '.') *p++ = *fen; else {
        *p++ = ':';
        *p++ = table[2* (*fen - 'A' - n)] + n;
        *p++ = table[2* (*fen - 'A' - n)+1] + n;
      }
    } else *p++ = *fen;
    fen++;
  }
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
  SetUp(array, currentVariant);
  sup0 = sup1 = sup2 = ABSENT;
  rootEval = cnt50 = hashKeyH = hashKeyL = moveNr = 0;
  return stm;
}

void
SetMemorySize (int n)
{
}

char *
MoveToText (MOVE move, int multiLine)
{
  static char buf[50];
  int f = move>>SQLEN & SQUARE, g = f, t = move & SQUARE;
  if(f == t) { sprintf(buf, "@@@@"); return buf; } // null-move notation in WB protocol
  buf[0] = '\0';
  if(t >= SPECIAL) { // kludgy! Print as side effect non-standard WB command to remove victims from double-capture (breaks hint command!)
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
    t = g + toList[t - SPECIAL];
  }
  sprintf(buf+strlen(buf), "%c%d%c%d%s", f%BW+'a', f/BW+ONE, t%BW+'a', t/BW+ONE, move & PROMOTE ? "+" : "");
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
  }
  ret = f<<SQLEN | t2;
  if(*moveText == '+') ret |= PROMOTE;
printf("# suppress = %c%d\n", sup1%BW+'a', sup1/BW);
MapFromScratch(attacks);
  postThinking--; repCnt = 0;
  Search(-INF-1, INF+1, 0, 1, sup1 & ~PROMOTE, sup2);
  postThinking++;
  for(i=retFirst; i<retMSP; i++) {
    if(moveStack[i] == INVALID) continue;
    if(c == '@' && (moveStack[i] & SQUARE) == (moveStack[i] >> SQLEN & SQUARE)) break; // any null move matches @@@@
    if((moveStack[i] & (PROMOTE | DEFER-1)) == ret) break;
    if((moveStack[i] & DEFER-1) == ret) deferred = i; // promoted version of entered non-promotion is legal
  }
  if(i>=retMSP) {  // no exact match
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
    if(i >= retMSP) {
      for(i=retFirst; i<retMSP; i++) printf("# %d. %08x %08x %s\n", i-50, moveStack[i], ret, MoveToText(moveStack[i], 0));
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
  return (i >= retMSP ? INVALID : moveStack[i]);
}

void
Highlight(char *coords)
{
  int i, j, n, sqr, cnt=0;
  char b[BSIZE], buf[2000], *q;
  for(i=0; i<bsize; i++) b[i] = 0;
  ReadSquare(coords, &sqr);
MapFromScratch(attacks);
//pmap(attacks, WHITE);
//pmap(attacks, BLACK);
//flag=1;
  postThinking--; repCnt = 0;
  Search(-INF-1, INF+1, 0, 1, sup1 & ~PROMOTE, sup2);
  postThinking++;
flag=0;
  for(i=retFirst; i<retMSP; i++) {
    if(sqr == (moveStack[i]>>SQLEN & SQUARE)) {
      int t = moveStack[i] & SQUARE;
      if(t >= SPECIAL) continue;
      b[t] = (board[t] == EMPTY ? 'Y' : 'R'); cnt++;
    }
  }
  if(!cnt) { // no moves from given square
    if(sqr != lastPut) return; // refrain from sending empty FEN
    // we lifted a piece for second leg of move
    for(i=retFirst; i<retMSP; i++) {
      if(lastLift == (moveStack[i]>>SQLEN & SQUARE)) {
	int e, t = moveStack[i] & SQUARE;
	if(t < SPECIAL) continue; // only special moves
	e = lastLift + epList[t - SPECIAL]; // decode
	t = lastLift + toList[t - SPECIAL];
	if(e != sqr) continue;
	b[t] = (board[t] == EMPTY ? 'Y' : 'R'); cnt++;
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

int
SearchBestMove (int stm, int timeLeft, int mps, int timeControl, int inc, int timePerMove, MOVE *move, MOVE *ponderMove)
{
  int score, targetTime, movesLeft = BW*BH/4 + 20;
  if(mps) movesLeft = mps - (moveNr>>1)%mps;
  targetTime = timeLeft*10 / (movesLeft + 2) + 1000 * inc;
  if(moveNr < 30) targetTime *= 0.5 + moveNr/60.; // speedup in opening
  if(timePerMove > 0) targetTime = timeLeft * 5;
  startTime = GetTickCount();
  tlim1 = 0.2*targetTime;
  tlim2 = 1.9*targetTime;
  nodes = 0;
MapFromScratch(attacks);
  retMove = INVALID; repCnt = 0;
  score = Search(-INF-1, INF+1, rootEval, maxDepth, sup1, sup2);
  *move = retMove;
  *ponderMove = INVALID;
  return score;
}

void
PonderUntilInput (int stm)
{
}

    int TakeBack(int n)
    { // reset the game and then replay it to the desired point
      int last, stm;
      Init(currentVariant); stm = Setup2(NULL);
printf("# setup done");fflush(stdout);
      last = moveNr - n; if(last < 0) last = 0;
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

    main()
    {
      int engineSide=NONE;                     // side played by engine
      int timeLeft;                            // timeleft on engine's clock
      int mps, timeControl, inc, timePerMove;  // time-control parameters, to be used by Search
      MOVE move, ponderMove;
      int i, score;
      char inBuf[8000], command[80];

  Init(V_CHU); // Chu

      while(1) { // infinite loop

        fflush(stdout);                 // make sure everything is printed before we do something that might take time

        if(stm == engineSide) {         // if it is the engine's turn to move, set it thinking, and let it move
     
          score = SearchBestMove(stm, timeLeft, mps, timeControl, inc, timePerMove, &move, &ponderMove);

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
            stm = MakeMove2(stm, move);  // assumes MakeMove returns new side to move
            gameMove[moveNr++] = move;  // remember game
            printf("move %s\n", MoveToText(move, 1));
          }
        }

        fflush(stdout); // make sure everything is printed before we do something that might take time

        // now it is not our turn (anymore)
        if(engineSide == ANALYZE) {       // in analysis, we always ponder the position
            PonderUntilInput(stm);
        } else
        if(engineSide != NONE && ponder == ON && moveNr != 0) { // ponder while waiting for input
          if(ponderMove == INVALID) {     // if we have no move to ponder on, ponder the position
            PonderUntilInput(stm);
          } else {
            int newStm = MakeMove2(stm, ponderMove);
            PonderUntilInput(newStm);
            UnMake2(ponderMove);
          }
        }

      noPonder:
        // wait for input, and read it until we have collected a complete line
        for(i = 0; (inBuf[i] = getchar()) != '\n'; i++);
        inBuf[i+1] = 0;
pboard(board);
pmoves(retFirst, retMSP);

        // extract the first word
        sscanf(inBuf, "%s", command);
printf("in: %s\n", command);

        // recognize the command,and execute it
        if(!strcmp(command, "quit"))    { break; } // breaks out of infinite loop
        if(!strcmp(command, "force"))   { engineSide = NONE;    continue; }
        if(!strcmp(command, "analyze")) { engineSide = ANALYZE; continue; }
        if(!strcmp(command, "exit"))    { engineSide = NONE;    continue; }
        if(!strcmp(command, "otim"))    { goto noPonder; } // do not start pondering after receiving time commands, as move will follow immediately
        if(!strcmp(command, "time"))    { sscanf(inBuf, "time %d", &timeLeft); goto noPonder; }
        if(!strcmp(command, "level"))   {
          int min, sec=0;
          sscanf(inBuf, "level %d %d %d", &mps, &min, &inc) == 3 ||  // if this does not work, it must be min:sec format
          sscanf(inBuf, "level %d %d:%d %d", &mps, &min, &sec, &inc);
          timeControl = 60*min + sec; timePerMove = -1;
          continue;
        }
        if(!strcmp(command, "protover")){
          printf("feature ping=1 setboard=1 colors=0 usermove=1 memory=1 debug=1 sigint=0 sigterm=0\n");
          printf("feature variants=\"normal,chu,dai,tenjiku,12x12+0_fairy,9x9+0_shogi\"\n");
          printf("feature myname=\"HaChu " VERSION "\" highlight=1\n");
          printf("feature option=\"Resign -check 0\"\n");           // example of an engine-defined option
          printf("feature option=\"Contempt -spin 0 -200 200\"\n"); // and another one
          printf("feature done=1\n");
          continue;
        }
        if(!strcmp(command, "option")) { // setting of engine-define option; find out which
          if(sscanf(inBuf+7, "Resign=%d",   &resign)         == 1) continue;
          if(sscanf(inBuf+7, "Contempt=%d", &contemptFactor) == 1) continue;
          continue;
        }
        if(!strcmp(command, "variant")) {
          for(i=0; i<5; i++) {
            sscanf(inBuf+8, "%s", command);
            if(!strcmp(variants[i].name, command)) {
printf("var %d\n",i);
              Init(i); stm = Setup2(NULL); break;
            }
	  }
          continue;
        }
        if(!strcmp(command, "sd"))      { sscanf(inBuf, "sd %d", &maxDepth);    continue; }
        if(!strcmp(command, "st"))      { sscanf(inBuf, "st %d", &timePerMove); continue; }
        if(!strcmp(command, "memory"))  { SetMemorySize(atoi(inBuf+7)); continue; }
        if(!strcmp(command, "ping"))    { printf("pong%s", inBuf+4); continue; }
    //  if(!strcmp(command, ""))        { sscanf(inBuf, " %d", &); continue; }
        if(!strcmp(command, "new"))     {
          engineSide = BLACK; Init(V_CHESS); stm = Setup2(NULL); maxDepth = MAXPLY; randomize = OFF; comp = 0;
          continue;
        }
        if(!strcmp(command, "setboard")){ engineSide = NONE;  Init(currentVariant); stm = Setup2(inBuf+9); continue; }
        if(!strcmp(command, "easy"))    { ponder = OFF; continue; }
        if(!strcmp(command, "hard"))    { ponder = ON;  continue; }
        if(!strcmp(command, "undo"))    { stm = TakeBack(1); continue; }
        if(!strcmp(command, "remove"))  { stm = TakeBack(2); continue; }
        if(!strcmp(command, "go"))      { engineSide = stm;  continue; }
        if(!strcmp(command, "post"))    { postThinking = ON; continue; }
        if(!strcmp(command, "nopost"))  { postThinking = OFF;continue; }
        if(!strcmp(command, "random"))  { randomize = ON;    continue; }
        if(!strcmp(command, "hint"))    { if(ponderMove != INVALID) printf("Hint: %s\n", MoveToText(ponderMove, 0)); continue; }
        if(!strcmp(command, "lift"))    { Highlight(inBuf+5); continue; }
        if(!strcmp(command, "put"))     { ReadSquare(inBuf+4, &lastPut); continue; }
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
        if(!strcmp(command, "result"))  { continue; }
        if(!strcmp(command, "hover"))   {  continue; }
        if(!strcmp(command, ""))  {  continue; }
        if(!strcmp(command, "usermove")){
          int move = ParseMove(inBuf+9);
          if(move == INVALID) {
            if(reason) printf("Illegal move {%s}\n", reason); else printf("%s\n", reason="Illegal move");
            if(comp) PrintResult(stm, -INF); // against computer: claim
          } else {
            stm = MakeMove2(stm, move);
            ponderMove = INVALID;
            gameMove[moveNr++] = move;  // remember game
          }
          continue;
        }
        printf("Error: unknown command\n");
      }
    }

