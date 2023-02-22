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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "piece.h"
#include "variant.h"

#define HASH
#define KILLERS
#define NULLMOVE
#define CHECKEXT
#define LMR 4
#define PATH 0
#define QSDEPTH 4
#define LIONTRAP
#define KINGSAFETY
#define KSHIELD
#define FORTRESS
#define PAWNBLOCK
#define TANDEM 100 /* bonus for pairs of attacking light steppers */
#define KYLIN 100 /* extra end-game value of Kylin for promotability */
#define PROMO 0 /* extra bonus for 'vertical' piece when it actually promotes (diagonal pieces get half) */

char *array, *IDs;
int bFiles, bRanks, zone, currentVariant, repDraws, stalemate;

int tsume, pvCuts, allowRep, entryProm=1, okazaki, pVal;
int stm, xstm, hashKeyH=1, hashKeyL=1, framePtr, msp, nonCapts, rootEval, filling, promoDelta;
int level, cnt50, mobilityScore;

Vector direction[2*RAYS] = { // clockwise!
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
int kingStep[RAYS+2], knightStep[RAYS+2]; // raw tables for step vectors (indexed as -1 .. 8)
int neighbors[RAYS+1];                    // similar to kingStep, but starts with null-step
Flag fireFlags[10]; // flags for Fire-Demon presence (last two are dummies, which stay 0, for compactify)

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

int pieces[COLORS], royal[COLORS], kylin[COLORS];
PieceInfo p[NPIECES]; // piece list

int squareKey[BSIZE];
int board[BSIZE] = { [0 ... BSIZE-1] = EDGE };

int attacksByLevel[LEVELS][COLORS][BSIZE];

char promoBoard[BSIZE] = { [0 ... BSIZE-1] = 0 }; // flags to indicate promotion zones
Flag fireBoard[BSIZE]; // flags to indicate squares controlled by Fire Demons
signed char PST[PSTSIZE] = { 0 };

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
    case V_WA: // Wa
      return ListLookUp(name, waPieces);
    case V_WOLF: // Werewolf
      return ListLookUp(name, wolfPieces);
    case V_CASHEW: // Cashew
      return ListLookUp(name, ddPieces);
    case V_MACAD: // Cashew
      return ListLookUp(name, makaPieces);
  }
  return NULL;
}

void
DeletePiece (int stm, int n)
{ // remove piece number n from the mentioned side's piece list (and adapt the reference to the displaced pieces!)
  int i;
  for(i=stm+2; i<=pieces[stm]; i+=2)
    if(p[i].promo > n) p[i].promo -= 2;
  for(i=n; i<pieces[stm]; i+=2) {
    p[i] = p[i+2];
    if(i+2 == royal[stm]) royal[stm] -= 2; // NB: squeezing out the King moves up Crown Prince to royal[stm]
    if(i+2 == kylin[stm]) kylin[stm] -= 2;
    if(i < 10) fireFlags[i-2] = fireFlags[i];
  }
  pieces[stm] -= 2;
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
  if(r[0] == X) return 30 + PROMO*((unsigned int)(r[1] | r[2] | r[3] | r[5] | r[6] | r[7]) <= 1);
  if(r[1] == X || r[7] == X) return 30 + PROMO/2;
  return 0;
}

Flag
IsUpwardCompatible (signed char *r, signed char *s)
{
  int i;
  for(i=0; i<RAYS; i++) {
    if(Worse(r[i], s[i])) return 0;
  }
  return 1;
}

int
ForwardOnly (signed char *range)
{
  int i;
  for(i=2; i<RAYS-1; i++) if(range[i]) return 0; // sideways and/or backwards
  return 1;
}

int
Range (signed char *range)
{
  int i, m=0;
  for(i=0; i<RAYS; i++) {
    int d = range[i];
    if(d < 0) d = (d >= L ? 2 : 36);
    m = MAX(m, d);
  }
  return m;
}

int multis[2], multiMovers[100];

void
StackMultis (int col)
{
  int i, j;
  multis[col] = col;
  for(i=col+2; i<=pieces[col]; i+=2) { // scan piece list for multi-capturers
    for(j=0; j<RAYS; j++) if(p[i].range[j] < J && p[i].range[j] >= S || p[i].value == FVAL) {
      multiMovers[multis[col]] = i; // found one: put its piece number in list
      multis[col] += 2;
      break;
    }
  }
}

void
Compactify (int stm)
{ // remove pieces that are permanently gone (captured or promoted) from one side's piece list
  int i, k;
  for(i=stm+2; i<=pieces[stm]; i+=2) { // first pass: unpromoted pieces
    if((k = p[i].promo) >= 0 && p[i].pos == ABSENT) { // unpromoted piece no longer there
      p[k].promo = -2; // orphan promoted version
      DeletePiece(stm, i);
    }
  }
  for(i=stm+2; i<=pieces[stm]; i+=2) { // second pass: promoted pieces
    if((k = p[i].promo) == -2 && p[i].pos == ABSENT) { // orphaned promoted piece not present
      DeletePiece(stm, i);
    }
  }
  StackMultis(stm);
}

int
AddPiece (int stm, PieceDesc *list)
{
  int i, j, *key, v;
  for(i=stm+2; i<=pieces[stm]; i += 2) {
    if(p[i].value < list->value || p[i].value == list->value && (p[i].promo < 0)) break;
  }
  pieces[stm] += 2;
  for(j=pieces[stm]; j>i; j-= 2) p[j] = p[j-2];
  p[i].value = v = list->value;
  for(j=0; j<RAYS; j++) p[i].range[j] = list->range[j^(RAYS/2)*(WHITE-stm)];
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
  p[i].ranking = list->ranking;
  p[i].mobWeight = v > 600 ? 0 : v >= 400 ? 1 : v >= 300 ? 2 : v > 150 ? 3 : v >= 100 ? 2 : 0;
  if(Lance(list->range))
    p[i].mobWeight = 0, p[i].pst = list->range[4] ? PST_NEUTRAL : PST_LANCE; // keep back
  for(j=stm+2; j<= pieces[stm]; j+=2) {
    if(p[j].promo >= i) p[j].promo += 2;
  }
  if(royal[stm] >= i) royal[stm] += 2;
  if(kylin[stm] >= i) kylin[stm] += 2;
  if(p[i].value == (currentVariant == V_SHO || currentVariant == V_WA ? 410 : 280) ) royal[stm] = i, p[i].pst = 0;
  p[i].qval = (tenFlag ? list->ranking : 0); // jump-capture hierarchy
  return i;
}

void
SetUp (char *array, int var)
{
  int i, j, n, m, color, c;
  char name[3], prince = 0;
  PieceDesc *p1, *p2;
  pieces[WHITE] = 1; pieces[BLACK] = 0;
  royal[WHITE] = royal[BLACK] = 0;
  for(i=bRanks-1; ; i--) {
//printf("next rank: %s\n", array);
    for(j = bFiles*i; ; j++) {
      int pflag=0;
      if(*array == '+') pflag++, array++;
      c = name[0] = *array++;
      if(!c) goto eos;
      if(c == '.') continue;
      if(c > '0' && c <= '9') {
        c -= '0'; if(*array >= '0' && *array <= '9') c = 10*c + *array++ - '0';
        j += c - 1; continue;
      }
      if(c == '/') break;
      name[1] = name[2] = 0;
      if(c >= 'a') {
	color = BLACK;
	c += 'A' - 'a';
      } else color = WHITE;
      if(*array == '\'') c += 26, array++; else
      if(*array == '!')  c += 52, array++;
      name[0] = IDs[2*(c - 'A')];
      name[1] = IDs[2*(c - 'A') + 1]; if(name[1] == ' ') name[1] = 0;
      if(!strcmp(name, "CP") || pflag && !strcmp(name, "DE")) prince |= color+1; // remember if we added Crown Prince
      p1 = LookUp(name, var);
      if(!p1) printf("tellusererror Unknown piece '%s' in setup (%d)\n", name, c), exit(-1);
      if(pflag && p1->promoted) p1 = LookUp(p1->promoted, var); // use promoted piece instead
      n = AddPiece(color, p1);
      p[n].pos = POS(j / bFiles, j % bFiles);
      if(p1->promoted[0] && !pflag) {
        if(!strcmp(p1->promoted, "CP")) prince |= color+1; // remember if we added Crown Prince
	p2 = LookUp(p1->promoted, var);
        m = AddPiece(color, p2);
	if(m <= n) n += 2;
	if(p2->ranking > 5) { // contageous
	  AddPiece(color, p2);
	  if(m <= n) n += 2;
	}
	p[n].promo = m;
	p[n].promoFlag = IsUpwardCompatible(p2->range, p1->range) * DONT_DEFER + CAN_PROMOTE;
	if(ForwardOnly(p1->range)) p[n].promoFlag |= LAST_RANK; // Pieces that only move forward can't defer on last rank
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
  for(i=0; i<RAYS; i++)  fireFlags[i] = 0;
  for(i=2, n=1; i<10; i++) if(p[i].value == FVAL) {
    int x = p[i].pos; // mark all burn zones
    fireFlags[i-2] = n;
    if(x != ABSENT) for(j=0; j<RAYS; j++) fireBoard[x+kStep[j]] |= n;
    n <<= 1;
  }
  for(i=2; i<6; i++) if(p[i].ranking == 5) p[i].promo = -1, p[i].promoFlag = 0; // take promotability away from Werewolves
  for(i=0; i<bRanks; i++) for(j=0; j<bFiles; j++) board[POS(i, j)] = EMPTY;
  for(i=WHITE+2; i<=pieces[WHITE]; i+=2) if(p[i].pos != ABSENT) {
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
  for(i=BLACK+2; i<=pieces[BLACK]; i+=2) if(p[i].pos != ABSENT) {
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
  bFiles = variants[var].boardFiles;
  bRanks = variants[var].boardRanks;
  zone   = variants[var].zoneDepth;
  array  = variants[var].array;
  IDs    = variants[var].IDs;
  }
  stalemate = (chessFlag || currentVariant == V_MAKRUK || currentVariant == V_LION || currentVariant == V_WOLF);
  repDraws  = (stalemate || currentVariant == V_SHATRANJ);
  pawn = LookUp("P", currentVariant); pVal = pawn ? pawn->value : 0; // get Pawn value

  for(i=-1; i<RAYS+1; i++) { // board steps in linear coordinates
    kStep[i] = STEP(direction[i&7].x,        direction[i&7].y);        // King
    nStep[i] = STEP(direction[(i&7)+RAYS].x, direction[(i&7)+RAYS].y); // Knight
  }
  for(i=0; i<RAYS; i++) neighbors[i+1] = kStep[i];

  for(i=0; i<RAYS; i++) { // Lion double-move decoding tables
    for(j=0; j<RAYS; j++) {
      epList[RAYS*i+j] = kStep[i];
      toList[RAYS*i+j] = kStep[j] + kStep[i];
      for(k=0; k<RAYS*i+j; k++)
	if(epList[k] == toList[RAYS*i+j] && toList[k] == epList[RAYS*i+j])
	  reverse[k] = RAYS*i+j, reverse[RAYS*i+j] = k;
    }
    // Lion-Dog triple moves
    toList[64+i] = 3*kStep[i]; epList[64+i] =   kStep[i];
    toList[72+i] = 3*kStep[i]; epList[72+i] = 2*kStep[i];
    toList[80+i] = 3*kStep[i]; epList[80+i] = 2*kStep[i]; ep2List[80+i] = kStep[i];
    toList[88+i] =   kStep[i]; epList[88+i] = 2*kStep[i];
  }
  // castling (king square, rook squares)
  toList[100]   = LR - 1; epList[100]   = LR; ep2List[100]   = LR - 2;
  toList[100+1] = LL + 2; epList[100+1] = LL; ep2List[100+1] = LL + 3;
  toList[100+2] = UR - 1; epList[100+2] = UR; ep2List[100+2] = UR - 2;
  toList[100+3] = UL + 2; epList[100+1] = UL; ep2List[100+1] = UL + 3;

  // hash key tables
  for(i=0; i<BSIZE; i++) squareKey[i] = ~(myRandom()*myRandom());

  // promotion zones
  for(i=0; i<bRanks; i++) for(j=0; j<bFiles; j++) {
    char v = 0;
    if(i == 0)           v |= LAST_RANK & P_BLACK;
    if(i < 2)            v |= CANT_DEFER & P_BLACK;
    if(i < zone)         v |= (CAN_PROMOTE | DONT_DEFER) & P_BLACK; else v &= ~P_BLACK;
    if(i >= bRanks-zone) v |= (CAN_PROMOTE | DONT_DEFER) & P_WHITE; else v &= ~P_WHITE;
    if(i >= bRanks-2)    v |= CANT_DEFER & P_WHITE;
    if(i == bRanks-1)    v |= LAST_RANK & P_WHITE;
    promoBoard[POS(i, j)] = v;
  }

  // piece-square tables
  for(i=0; i<bRanks; i++) {
   for(j=0; j<bFiles; j++) {
    int s = POS(i, j), d = bRanks*(bRanks-2) - abs(2*i - bRanks + 1)*(bRanks-1) - (2*j - bRanks + 1)*(2*j - bRanks + 1);
    PST[s] = 2*(i==0 | i==bRanks-1) + (i==1 | i==bRanks-2); // last-rank markers in null table
    PST[PST_STEPPER+s] = d/4 - (i < 2 || i > bRanks-3 ? 3 : 0) - (j == 0 || j == bFiles-1 ? 5 : 0)
                    + 3*(i==zone || i==bRanks-zone-1);      // stepper centralization
    PST[PST_WJUMPER+s] = d/6;                               // double-stepper centralization
    PST[PST_SLIDER +s] = d/12 - 15*(i==bRanks/2 || i==(bRanks-1)/2);// slider centralization
    PST[PST_TRAP  +s] = j < 3 || j > bRanks-4 ? (i < 3 ? 7 : i == 3 ? 4 : i == 4 ? 2 : 0) : 0;
    PST[PST_CENTER+s] = ((bRanks-1)*(bRanks-1) - (2*i - bRanks + 1)*(2*i - bRanks + 1) - (2*j - bRanks + 1)*(2*j - bRanks + 1))/6;
    PST[PST_WPPROM+s] = PST[PST_BPPROM+s] = PST[PST_STEPPER+s]; // as stepper, but with pre-promotion bonus W/B
    PST[PST_BJUMPER+s] = PST[PST_WJUMPER+s];                // as jumper, but with pre-promotion bonus B
    PST[PST_ZONDIST+s] = BW*(zone - 1 - i);                 // board step to enter promo zone black
    PST[PST_ADVANCE+s] = PST[PST_WFLYER-s-1] = 2*(5*i+i*i) - (i >= zone)*6*(i-zone+1)*(i-zone+1)
	- (2*j - bRanks + 1)*(2*j - bRanks + 1)/bRanks + bRanks/2
	- 50 - 35*(j==0 || j == bRanks-1) - 15*(j == 1 || bRanks-2); // advance-encouraging table
    PST[PST_WFLYER +s] = PST[PST_LANCE-s-1] = (i == zone-1)*40 + (i == zone-2)*20 - 20;
    PST[PST_LANCE  +s] = (PST[PST_STEPPER+j] - PST[PST_STEPPER+s])/2;
   }
   if(zone > 0)
	PST[PST_WPPROM+STEP(bRanks-1-zone, j)] += 10, PST[PST_BPPROM + STEP(zone, j)] += 10;
   if(j > (bRanks-1)/2 - 3 && j < bRanks/2 + 3)
	PST[PST_WPPROM + j] += 4, PST[PST_BPPROM + POS(bRanks-1, j)] += 4; // fortress
   if(j > (bRanks-1)/2 - 2 && j < bRanks/2 + 2)
	PST[PST_WPPROM + POS(1, j)] += 2, PST[PST_BPPROM + POS(bRanks-2, j)] += 2; // fortress
#if KYLIN
   // pre-promotion bonuses for jumpers
   if(zone > 0) PST[PST_WJUMPER + POS(bRanks-2-zone, j)] = PST[PST_BJUMPER + POS(zone+1, j)] = 100,
                PST[PST_WJUMPER + POS(bRanks-1-zone, j)] = PST[PST_BJUMPER + POS(zone, j)] = 200;
#endif
  }

  p[EDGE].qval = 5; // tenjiku jump-capturer sentinel
}

int
PSTest ()
{
  int r, f, score, tot=0;
  for(r=0; r<bRanks; r++) for(f=0; f<bFiles; f++) {
    int s = POS(r, f);
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
  for(r=0; r<bRanks; r++) for(f=0; f<bFiles; f++) {
    int s = POS(r, f);
    int piece = board[s];
    if(!piece) continue;
    score = p[piece].promoGain;
    if(piece & 1) tot += score; else tot -= score;
  }
  return tot;
}

int
MapAttacksByColor (int color, int pieces)
{
  bzero(attacks[color], sizeof(attacks[color]));
  int i, j, totMob = 0;
  for(i=color+2; i<=pieces; i+=2) {
    int mob = 0;
    const PieceInfo *pi = &(p[i]);
    if(pi->pos == ABSENT) continue;
    for(j=0; j<RAYS; j++) {
      int x = pi->pos, v = kStep[j], r = pi->range[j];
      if(r < 0) { // jumping piece, special treatment
	if(r == N) {
	  x += nStep[j];
	  if(board[x] != EMPTY && board[x] != EDGE)
	    ATTACK(x, color) += ray[RAYS];
	} else
	if(r >= S) { // in any case, do a jump of 2
	  if(board[x + 2*v] != EMPTY && board[x + 2*v] != EDGE)
	    ATTACK((x + 2*v), color) += ray[j], mob += (board[x + 2*v] ^ color) & 1;
	  if(r < J) { // more than plain jump
	    if(board[x + v] != EMPTY && board[x + v] != EDGE)
	      ATTACK((x + v), color) += ray[j]; // single step (completes D and I)
	    if(r < I) {  // Lion power
	    if(r >= T) { // Lion Dog, also do a jump of 3
	      if(board[x + 3*v] != EMPTY && board[x + 3*v] != EDGE)
		ATTACK((x + 3*v), color) += ray[j];
	      if(r == K) { // Teaching King also range move
		int y = x, n = 0;
		while(1) {
		  if(board[y+=v] == EDGE) break;
 		  if(board[y] != EMPTY) {
		    if(n > 2) ATTACK(y, color) += ray[j]; // outside Lion range
		    break;
		  }
		  n++;
		}
	      }
	    } else
	    if(r <= L) {  // true Lion, also Knight jump
	      if(r < L) { // Lion plus (limited) range
		int y = x, n = 0;
		int rg = (r == S ? 36 : 3);
		while(n++ < rg) {
		  if(board[y+=v] == EDGE) break;
		  if(board[y] != EMPTY) {
		    if(n > 2) ATTACK(y, color) += ray[j]; // outside Lion range
		    break;
		  }
		}
	      }
	      v = nStep[j];
	      if(board[x + v] != EMPTY && board[x + v] != EDGE && r != W)
		ATTACK((x + v), color) += ray[RAYS];
	    }
	    }
	  }
	} else
	if(r == C) { // FIDE Pawn diagonal
	  if(board[x + v] != EMPTY && board[x + v] != EDGE)
	    ATTACK((x + v), color) += ray[j];
	}
	continue;
      }
      for(int y=x; r-- > 0 && board[y+=v] != EDGE; ) {
        mob += dist(y, x);
        ATTACK(y, color) += ray[j], mob += (board[y] ^ color) & 1;
        if(pi->range[j] > X) { // jump capturer
          int c = pi->qval;
          if(p[board[y]].qval < c) {
            y += v; // go behind directly captured piece, if jumpable
            while(p[board[y]].qval < c) { // kludge alert: EDGE has qval = 5, blocking everything
              if(board[y] != EMPTY) {
//              int n = ATTACK(y, color) & attackMask[j];
//              ATTACK(y, color) += (n < 3*one[j] ? 3*one[j] : ray[j]); // first jumper gets 2 extra (to ease incremental update)
                ATTACK(y, color) += ray[j]; // for now use true count
              }
              y += v;
            }
          }
        }
        if(board[y] != EMPTY) break;
      }
    }
    totMob += mob * pi->mobWeight;
  }
if(!level) printf("# mobility %d = %d\n", color, totMob);
  return totMob;
}

void
MapAttacks ()
{
  mobilityScore  = MapAttacksByColor(WHITE, pieces[WHITE]);
  mobilityScore -= MapAttacksByColor(BLACK, pieces[BLACK]);
}

int
MakeMove (Move m, UndoInfo *u)
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
    for(i=0; i<RAYS; i++) fireBoard[u->from + kStep[i]] &= f; // clear old burn zone
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
    u->to       = u->from + toList[u->to - SPECIAL];
    hashKeyL ^= p[u->epVictim[0]].pieceKey * squareKey[u->epSquare];
    hashKeyH ^= p[u->epVictim[0]].pieceKey * squareKey[u->epSquare + STEP(1, 0)];
    hashKeyL ^= p[u->epVictim[0]].pieceKey * squareKey[u->ep2Square];
    hashKeyH ^= p[u->epVictim[0]].pieceKey * squareKey[u->ep2Square + STEP(1, 0)];
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
    hashKeyH ^= p[u->epVictim[0]].pieceKey * squareKey[u->epSquare + STEP(1, 0)];
    hashKeyL ^= p[u->epVictim[1]].pieceKey * squareKey[u->ep2Square];
    hashKeyH ^= p[u->epVictim[1]].pieceKey * squareKey[u->ep2Square + STEP(1, 0)];
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
    for(i=0; i<RAYS; i++) {
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
	  hashKeyH ^= p[burnVictim].pieceKey * squareKey[x + STEP(1, 0)];
	  cnt50 = 0; // actually burning something makes the move irreversible
	}
    }
    u->epVictim[0] = EDGE; // kludge to flag to UnMake this is special move
  }

  u->victim = board[u->to];
  p[u->victim].pos = ABSENT;
  if(p[u->victim].ranking >= 5 && p[u->piece].ranking < 4) { // contageous piece captured by non-royal
    u->booty -= p[u->new].value;
    u->new = u->piece & 1 | (u->victim - 2 & ~3) + 2; // promote to it (assumes they head the list in pairs)
    if(p[u->new].pos != ABSENT) u->new += 2;
    p[u->piece].pos = ABSENT;
    u->booty += p[u->new].value;
  }

  u->booty += PST[p[u->new].pst + u->to] - PST[p[u->piece].pst + u->from];

  filling += p[u->new].bulk - p[u->piece].bulk - p[u->victim].bulk;
  promoDelta += p[u->new].promoGain - p[u->piece].promoGain + p[u->victim].promoGain;
  u->booty += p[u->victim].value + PST[p[u->victim].pst + u->to];
  u->gain  += p[u->victim].value;
  if(u->victim != EMPTY) {
    cnt50 = 0; // capture irreversible
    if(ATTACK(u->to, xstm)) u->loss = p[u->piece].value; // protected
  }

  p[u->new].pos = u->to;
  board[u->to] = u->new;
  promoDelta = -promoDelta;

  hashKeyL ^= p[u->new].pieceKey * squareKey[u->to]
           ^  p[u->piece].pieceKey * squareKey[u->from]
           ^  p[u->victim].pieceKey * squareKey[u->to];
  hashKeyH ^= p[u->new].pieceKey * squareKey[u->to + STEP(1, 0)]
           ^  p[u->piece].pieceKey * squareKey[u->from + STEP(1, 0)]
           ^  p[u->victim].pieceKey * squareKey[u->to + STEP(1, 0)];

  return deferred;
}

void
UnMake(UndoInfo *u)
{
  if(u->epVictim[0]) { // move with side effects
    if(u->epVictim[0] == EDGE) { // fire-demon burn
      int i, f=~fireFlags[u->piece-2];
      for(i=0; i<RAYS; i++) {
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
    for(i=0; i<RAYS; i++) fireBoard[u->from + kStep[i]] |= f; // restore old burn zone
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
pplist()
{
  int i, j;
  for(i=0; i<182; i++) {
	printf("%3d. %4d %3d %4d %02x %d %2d %x %3d %4d ", i, p[i].value, p[i].promo, p[i].pos, p[i].promoFlag&255, p[i].mobWeight, p[i].qval, p[i].bulk, p[i].promoGain, p[i].pst);
	for(j=0; j<RAYS; j++) printf(" %3d", p[i].range[j]);
	if(i<2 || i>11) printf("\n"); else printf("  %02x %d\n", fireFlags[i-2]&255, p[i].ranking);
  }
  printf("last: %d / %d\nroyal %d / %d\n", pieces[WHITE], pieces[BLACK], royal[WHITE], royal[BLACK]);
}

void
pboard (int *b)
{
  int i, j, p;
  for(i=0; i<bRanks; i++) {
    printf("#");
    for(j=0; j<bFiles; j++) {
      p=b[POS(i, j)];
      if(p) printf("%4d", p); else printf("    ");
    }
    printf("\n");
  }
}

void
pbytes (Flag *b)
{
  int i, j;
  for(i=0; i<bRanks; i++) {
    for(j=0; j<bFiles; j++) printf("%3x", b[POS(i, j)]);
    printf("\n");
  }
}

void
pmap (int color)
{
  // decode out of double-wide "attacks" array
  int i, j;
  for(i=bFiles-1; i>=0; i--) {
    printf("#");
    for(j=0; j<bRanks; j++) printf("%11o", ATTACK(POS(i, j), color));
    printf("\n");
  }
}

int
InCheck ()
{
  int k = p[royal[stm]].pos;
  if( k == ABSENT) k = p[royal[stm] + 2].pos;
  else if(p[royal[stm] + 2].pos != ABSENT) k = ABSENT; // two kings is no king...
  if( k != ABSENT) {
    MapAttacks();
    if(ATTACK(k, WHITE-stm)) return 1;
  }
  return 0;
}
