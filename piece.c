/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "eval.h"
#include "piece.h"
#include "types.h"
#include "variant.h"

#define PROMO 0 /* extra bonus for 'vertical' piece when it actually promotes (diagonal pieces get half) */

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
int pVal;             // value of pawn per variant

int squareKey[BSIZE];

Flag promoBoard[BSIZE] = { [0 ... BSIZE-1] = 0 }; // flags to indicate promotion zones

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
DeletePiece (Color c, int n)
{ // remove piece number n from the mentioned side's piece list (and adapt the reference to the displaced pieces!)
  int i;
  for(i=c+2; i<=pieces[c]; i+=2)
    if(p[i].promo > n) p[i].promo -= 2;
  for(i=n; i<pieces[c]; i+=2) {
    p[i] = p[i+2];
    if(i+2 == royal[c]) royal[c] -= 2; // NB: squeezing out the King moves up Crown Prince to royal[c]
    if(i+2 == kylin[c]) kylin[c] -= 2;
    if(i < 10) fireFlags[i-2] = fireFlags[i];
  }
  pieces[c] -= 2;
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
Lance (MoveType *r)
{ // File-bound forward slider
  int i;
  for(i=1; i<4; i++) if(r[i] || r[i+4]) return 0;
  return r[0] == X;
}

int
EasyProm (MoveType *r)
{
  if(r[0] == X) return 30 + PROMO*((unsigned int)(r[1] | r[2] | r[3] | r[5] | r[6] | r[7]) <= 1);
  if(r[1] == X || r[7] == X) return 30 + PROMO/2;
  return 0;
}

Flag
IsUpwardCompatible (MoveType *r, MoveType *s)
{
  int i;
  for(i=0; i<RAYS; i++) {
    if(Worse(r[i], s[i])) return 0;
  }
  return 1;
}

int
ForwardOnly (MoveType *range)
{
  int i;
  for(i=2; i<RAYS-1; i++) if(range[i]) return 0; // sideways and/or backwards
  return 1;
}

int
Range (MoveType *range)
{
  int i, m=0;
  for(i=0; i<RAYS; i++) {
    int d = range[i];
    if(d < 0) d = (d >= L ? 2 : 36);
    m = MAX(m, d);
  }
  return m;
}

void
Compactify (Color c)
{ // remove pieces that are permanently gone (captured or promoted) from one side's piece list
  int i, k;
  for(i=c+2; i<=pieces[c]; i+=2) { // first pass: unpromoted pieces
    if((k = p[i].promo) >= 0 && p[i].pos == ABSENT) { // unpromoted piece no longer there
      p[k].promo = -2; // orphan promoted version
      DeletePiece(c, i);
    }
  }
  for(i=c+2; i<=pieces[c]; i+=2) { // second pass: promoted pieces
    if((k = p[i].promo) == -2 && p[i].pos == ABSENT) { // orphaned promoted piece not present
      DeletePiece(c, i);
    }
  }
  StackMultis(c);
}

int
AddPiece (Color c, PieceDesc *list)
{
  int i, j, *key, v;
  for(i=c+2; i<=pieces[c]; i += 2) {
    if(p[i].value < list->value || p[i].value == list->value && (p[i].promo < 0)) break;
  }
  pieces[c] += 2;
  for(j=pieces[c]; j>i; j-= 2) p[j] = p[j-2];
  p[i].value = v = list->value;
  for(j=0; j<RAYS; j++) p[i].range[j] = list->range[j^(RAYS/2)*(WHITE-c)];
  switch(Range(p[i].range)) {
    case 1:  p[i].pst = PST_STEPPER; break;
    case 2:  p[i].pst = PST_JUMPER; break;
    default: p[i].pst = PST_SLIDER;  break;
  }
  key = (c == WHITE ? &list->whiteKey : &list->blackKey);
  if(!*key) *key = ~(myRandom()*myRandom());
  p[i].promoGain = EasyProm(list->range); // flag easy promotion based on white view
  p[i].pieceKey = *key;
  p[i].promoFlag = 0;
  p[i].bulk = list->bulk;
  p[i].ranking = list->ranking;
  p[i].mobWeight = v > 600 ? 0 : v >= 400 ? 1 : v >= 300 ? 2 : v > 150 ? 3 : v >= 100 ? 2 : 0;
  if(Lance(list->range))
    p[i].mobWeight = 0, p[i].pst = list->range[4] ? PST_NEUTRAL : PST_LANCE; // keep back
  for(j=c+2; j<= pieces[c]; j+=2) {
    if(p[j].promo >= i) p[j].promo += 2;
  }
  if(royal[c] >= i) royal[c] += 2;
  if(kylin[c] >= i) kylin[c] += 2;
  if(p[i].value == (currentVariant == V_SHO || currentVariant == V_WA ? 410 : 280) ) royal[c] = i, p[i].pst = PST_NEUTRAL;
  p[i].qval = (tenFlag ? list->ranking : 0); // jump-capture hierarchy
  return i;
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

  variant = &(variants[var]);
  if(var != SAME) { // the following should be already set if we stay in same variant (for TakeBack)
  currentVariant = variants[var].varNr;
  bFiles = variants[var].boardFiles;
  bRanks = variants[var].boardRanks;
  zone   = variants[var].zoneDepth;
  }
  stalemate = (chessFlag || makrukFlag || lionFlag || wolfFlag);
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
    Flag v = 0;
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
    PSQ(PST_NEUTRAL, s, BLACK) = 2*(i==0 | i==bRanks-1) + (i==1 | i==bRanks-2); // last-rank markers in null table
    PSQ(PST_STEPPER, s, BLACK) = d/4 - (i < 2 || i > bRanks-3 ? 3 : 0) - (j == 0 || j == bFiles-1 ? 5 : 0)
                    + 3*(i==zone || i==bRanks-zone-1);                          // stepper centralization
    PSQ(PST_JUMPER, s, BLACK) = d/6;                                            // double-stepper centralization
    PSQ(PST_SLIDER, s, BLACK) = d/12 - 15*(i==bRanks/2 || i==(bRanks-1)/2); // slider centralization
    PSQ(PST_TRAP  , s, BLACK) = j < 3 || j > bRanks-4 ? (i < 3 ? 7 : i == 3 ? 4 : i == 4 ? 2 : 0) : 0;
    PSQ(PST_CENTER, s, BLACK) = ((bRanks-1)*(bRanks-1) - (2*i - bRanks + 1)*(2*i - bRanks + 1) - (2*j - bRanks + 1)*(2*j - bRanks + 1))/6;
    PSQ(PST_PPROM , s, BLACK) = PSQ(PST_STEPPER, s, BLACK);                     // as stepper, but with pre-promotion bonus
    PSQ(PST_ZONDIST, s, BLACK) = BW*(zone - 1 - i);                             // board step to enter promo zone black
    PSQ(PST_ADVANCE, s, BLACK) = 2*(5*i+i*i) - (i >= zone)*6*(i-zone+1)*(i-zone+1)
	- (2*j - bRanks + 1)*(2*j - bRanks + 1)/bRanks + bRanks/2
	- 50 - 35*(j==0 || j == bRanks-1) - 15*(j == 1 || bRanks-2);            // advance-encouraging table
    PSQ(PST_FLYER, s, BLACK) = (i == zone-1)*40 + (i == zone-2)*20 - 20;
    PSQ(PST_LANCE, s, BLACK) = (PSQ(PST_STEPPER, j, BLACK) - PSQ(PST_STEPPER, s, BLACK))/2;
   }
   if(zone > 0)
	PSQ(PST_PPROM, POS(zone, j), BLACK) += 10;
   if(j > (bRanks-1)/2 - 3 && j < bRanks/2 + 3)
	PSQ(PST_PPROM, POS(bRanks-1, j), BLACK) += 4; // fortress
   if(j > (bRanks-1)/2 - 2 && j < bRanks/2 + 2)
	PSQ(PST_PPROM, POS(bRanks-2, j), BLACK) += 2; // fortress
#if KYLIN
   // pre-promotion bonuses for jumpers
   if(zone > 0) PSQ(PST_JUMPER, POS(zone+1, j), BLACK) = 100,
                PSQ(PST_JUMPER, POS(zone, j), BLACK) = 200;
#endif
  }

  p[EDGE].qval = 5; // tenjiku jump-capturer sentinel
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
