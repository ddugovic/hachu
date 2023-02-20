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

#include "hachu.h"
#include "variant.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>

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
DeletePiece (int n)
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

int
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
      DeletePiece(i);
    }
  }
  for(i=stm+2; i<=pieces[stm]; i+=2) { // second pass: promoted pieces
    if((k = p[i].promo) == -2 && p[i].pos == ABSENT) { // orphaned promoted piece not present
      DeletePiece(i);
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
  p[i].qval = (currentVariant == V_TENJIKU ? list->ranking : 0); // jump-capture hierarchy
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
  chuFlag = (currentVariant == V_CHU || currentVariant == V_LION);
  tenFlag = (currentVariant == V_TENJIKU);
  chessFlag = (currentVariant == V_CHESS || currentVariant == V_LION || currentVariant == V_WOLF);
  stalemate = (currentVariant == V_CHESS || currentVariant == V_MAKRUK || currentVariant == V_LION || currentVariant == V_WOLF);
  repDraws  = (stalemate || currentVariant == V_SHATRANJ);
  pawn = LookUp("P", currentVariant); pVal = pawn ? pawn->value : 0; // get Pawn value

  for(i=-1; i<9; i++) { // board steps in linear coordinates
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

static inline int
NewNonCapture (int x, int y, int promoFlags)
{
  if(board[y] != EMPTY) return 1; // edge, capture or own piece
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
#if 0
printf("last=%d nc=%d pf=%d move=%d\n", msp, nonCapts, promoFlags, x<<SQLEN | y);
#endif
  return 0;
}

static inline int
NewCapture (int x, int y, int promoFlags)
{
  if( (promoBoard[x] | promoBoard[(y < BSIZE ? y : x)]) & promoFlags) { // piece can promote with this move
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

char mapStep[] = { 7, 8, 1, -6, -7, -8, -1, 6 };
char rowMask[] = { 0100, 0140, 0160, 070, 034, 016, 07, 03, 01 };

void
AreaStep (int *map, int from, int x, int flags, int n, int d)
{
  int i;
  for(i=0; i<RAYS; i++) {
    int to = x + kStep[i], m = n + mapStep[i];
    if(board[to] == EDGE) continue; // off board
    if(map[m] >= d) continue;   // already done
    if(!map[m]) moveStack[msp++] = from<<SQLEN | to;
    map[m] = d;
    if(d > 1 && board[to] == EMPTY) AreaStep(map, from, to, flags, m, d-1);
  }
}

void
AreaMoves (int from, int piece, int range)
{
  int map[49]; // 7x7 map for area movers
  int i;
  for(i=0; i<49; i++) map[i] = 0;
  map[3*7+7] = range;
  AreaStep(map, from, from, p[piece].promoFlag, 3*7+3, range);
}

void
MarkBurns (int x)
{ // make bitmap of squares in FI (7x7) neighborhood where opponents can be captured or burned
  char rows[9];
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
GenCastlings ()
{ // castlings for Lion Chess. Assumes board width = 8 and Kings on e-file, and K/R value = 280/300!
    int f = POS(0, BW/2), t = CASTLE;
    if(stm != WHITE) f += (bRanks*BW) - BW, t += 2;
    if(p[board[f]].value = 280) {
      if(p[board[f-4]].value == 300 && board[f-3] == EMPTY && board[f-2] == EMPTY && board[f-1] == EMPTY) moveStack[msp++] = f<<SQLEN | t+1;
      if(p[board[f+3]].value == 300 && board[f+1] == EMPTY && board[f+2] == EMPTY) moveStack[msp++] = f<<SQLEN | t;
    }
    return msp;
}

int
GenNonCapts (int promoSuppress)
{
  int i, j, nullMove = ABSENT;
  for(i=stm+2; i<=pieces[stm]; i+=2) {
    int x = p[i].pos, pFlag = p[i].promoFlag;
    if(x == ABSENT) continue;
    if(x == promoSuppress && chuFlag) pFlag = 0;
    for(j=0; j<RAYS; j++) {
      int y, v = kStep[j], r = p[i].range[j];
      if(r < 0) { // jumping piece, special treatment
	if(r == N) { // pure Knight, do off-ray jump
	  NewNonCapture(x, x + nStep[j], pFlag);
	} else
	if(r >= S) { // in any case, do a jump of 2
	  int occup = NewNonCapture(x, x + 2*v, pFlag);
	  if(r < I) { // Lion power, also single step
	    if(!NewNonCapture(x, x + v, pFlag)) nullMove = (r == W ? ABSENT : x); else occup = 1;
	    if(r <= L) { // true Lion, also Knight jump
	      if(!occup & r < L) for(y=x+2*v; !NewNonCapture(x, y+=v, pFlag) && r == S; ); // BS and FF moves
	      v = nStep[j];
	      if(r != W) NewNonCapture(x, x + v, pFlag);
	    } else if(r >= T) { // T or K
	      occup |= NewNonCapture(x, x+3*v, pFlag); // Lion Dog, also triple step
	      if(!occup && r == K) for(y=x+3*v; !NewNonCapture(x, y+=v, pFlag); ); // Teaching King distant moves
	    }
	  } else if(r == I) NewNonCapture(x, x + v, pFlag); // also do step
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
GenCapts (int sqr, int victimValue)
{ // generate all moves that capture the piece on the given square
  int i, att = ATTACK(sqr, stm);
#if 0
printf("GenCapts(%c%d,%d) %08x\n", FILECH(sqr), RANK(sqr), victimValue, att);
#endif
  if(!att) return; // no attackers at all!
  for(i=0; i<RAYS; i++) {            // try all rays
    int x, v, jcapt=0;
    if(att & attackMask[i]) {        // attacked by move in this direction
      v = -kStep[i]; x = sqr;
      while( board[x+=v] == EMPTY ); // scan towards source until we encounter a 'stop'
#if 0
printf(" stop @ %c%d (dir %d)\n", FILECH(x), RANK(x), i);
#endif
      if((board[x] & TYPE) == stm) {               // stop is ours
	static int minRange[20] = {  3, 0, 0, 0, 2, 2,  2 }; // K, T, D, L, W, F, S
	static int maxRange[20] = { 36, 0, 0, 0, 3, 3, 36 }; // K, T, D, L, W, F, S
	int attacker = board[x], d = dist(x, sqr), r = p[attacker].range[i];
#if 0
printf("  attacker %d, range %d, dist %d\n", attacker, r, d);
#endif
	if(r >= d || r <= K && d <= maxRange[K-r] && d > minRange[K-r]) { // it has a plain move in our direction that hits us
	  NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	  att -= ray[i];
	  if(!(att & attackMask[i])) continue; // no more; next direction
	  jcapt = p[board[x]].qval;   // jump-capturer hierarchy
	  while(board[x+=v] == EMPTY);// one attack accounted for, but more to come, so skip to next stop
	}
      }
      // we get here when we are on a piece that does not attack us through a (limited) ranging move,
      // it can be our own or an enemy with not (enough) range, or which is blocked
      while(board[x] != EDGE) {
#if 0
printf("   scan %x-%x (%3d) dir=%d d=%d", sqr, x, board[x], i, dist(x, sqr)); fflush(stdout);
printf(" r=%d att=%o jcapt=%d qval=%d\n", p[board[x]].range[i], att, jcapt, p[board[x]].qval);
#endif
      if((board[x] & TYPE) == stm) {   // stop is ours
	int attacker = board[x], d = dist(x, sqr), r = p[attacker].range[i];
	if(jcapt < p[attacker].qval) { // it is a range jumper that jumps over the barrier
	  if(p[attacker].range[i] > 1) { // assumes all jump-captures are infinite range
	    NewCapture(x, sqr, p[attacker].promoFlag);
	    att -= ray[i];
	  }
#if 0
if(board[x] == EDGE) { printf("    edge hit %x-%x dir=%d att=%o\n", sqr, x, i, att); continue; }
#endif
	} else
	if(r < 0) { // stop has non-standard moves
	  switch(p[attacker].range[i]) { // figure out what he can do (including multi-captures, which causes the complexity)
	    case F: // Lion power + 3-step (as in FF)
	    case S: // Lion power + ranging (as in BS)
	    case L: // Lion
	      if(d > 2) break;
	      NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	      att -= ray[i];
	      // now the multi-captures of designated victim together with lower-valued piece
	      if(d == 2) { // primary victim on second ring; look for victims to take in passing
		if((board[sqr+v] & TYPE) == xstm && board[sqr+v] > board[sqr])
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
		if((i&1) == 0) { // orthogonal: two extra bent paths
		  v = kStep[i-1];
		  if((board[x+v] & TYPE) == xstm && board[x+v] > board[sqr])
		    NewCapture(x, SPECIAL + RAYS*(i-1&7) + (i+1&7) + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
		  v = kStep[i+1];
		  if((board[x+v] & TYPE) == xstm && board[x+v] > board[sqr])
		    NewCapture(x, SPECIAL + RAYS*(i+1&7) + (i-1&7) + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
		}
	      } else { // primary victim on first ring
		int j;
		for(j=0; j<RAYS; j++) { // we can go on in 8 directions after we captured it in passing
		  int v = kStep[j];
		  if(sqr+v == x || board[sqr+v] == EMPTY) { // hit & run; make sure we include igui (attacker is still at x!)
		    NewCapture(x, SPECIAL + RAYS*i + j + victimValue, p[attacker].promoFlag);
		  } else if((board[sqr+v] & TYPE) == xstm && board[sqr+v] > board[sqr]) {    // double capture
		    NewCapture(x, SPECIAL + RAYS*i + j + victimValue, p[attacker].promoFlag); // other victim after primary
		    if(dist(sqr+v, x) == 1) // other victim also on first ring; reverse order is possible
		      NewCapture(x, SPECIAL + reverse[RAYS*i + j] + victimValue, p[attacker].promoFlag);
		  }
		}
	      }
	      break;
	    case D: // linear Lion move (as in HF, SE)
	      if(d > 2) break;
	      NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	      att -= ray[i];
	      if(d == 2) { // check if we can take intermediate with it
		if((board[x-v] & TYPE) == xstm && board[x-v] > board[sqr])
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p.
	      } else { // d=1; can move on to second, or move back for igui
		NewCapture(x, SPECIAL + RAYS*i + (i^4) + victimValue, p[attacker].promoFlag); // igui
		if(board[sqr-v] == EMPTY || (board[sqr-v] & TYPE) == xstm && board[sqr-v] > board[sqr])
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // hit and run
	      }
	      break;
	    case T: // Lion-Dog move (awful!)
	    case K:
	      if(d > 3) break;
	      NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	      att -= ray[i];
	      if(d == 3) { // check if we can take one or two intermediates (with higher piece index) with it
		if((board[x-v] & TYPE) == xstm && board[x-v] > board[sqr]) {
		  NewCapture(x, SPECIAL + 64 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. first
		  if((board[x-2*v] & TYPE) == xstm && board[x-2*v] > board[sqr])
		    NewCapture(x, SPECIAL + 80 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. both
		} else if((board[x-2*v] & TYPE) == xstm && board[x-2*v] > board[sqr])
		  NewCapture(x, SPECIAL + 72 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. second
	      } else if(d == 2) { // check if we can take intermediate with it
		if((board[x-v] & TYPE) == xstm && board[x-v] > board[sqr]) {
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. first, stop at 2nd
		  NewCapture(x, SPECIAL + 88 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // shoot 2nd, take 1st
		  if(board[sqr-v] == EMPTY || (board[sqr-v] & TYPE) == xstm && board[sqr-v] > board[sqr])
		    NewCapture(x, SPECIAL + 80 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. 1st and 2nd
		} else if(board[sqr-v] == EMPTY || (board[sqr-v] & TYPE) == xstm && board[sqr-v] > board[sqr])
		  NewCapture(x, SPECIAL + 72 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. 2nd
	      } else { // d=1; can move on to second, or move back for igui
		NewCapture(x, SPECIAL + RAYS*i + (i^4) + victimValue, p[attacker].promoFlag); // igui
		if(board[sqr-v] == EMPTY) { // 2nd empty
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. 1st and run to 2nd
		  if(board[sqr-2*v] == EMPTY || (board[sqr-2*v] & TYPE) == xstm && board[sqr-2*v] > board[sqr])
		    NewCapture(x, SPECIAL + 72 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. 1st, end on 3rd
		} else if((board[sqr-v] & TYPE) == stm) { // 2nd own
		  if(board[sqr-2*v] == EMPTY || (board[sqr-2*v] & TYPE) == xstm && board[sqr-2*v] > board[sqr])
		    NewCapture(x, SPECIAL + 72 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. 1st, end on 3rd
		} else if((board[sqr-v] & TYPE) == xstm && board[sqr-v] > board[sqr]) {
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. 1st, capture and stop at 2nd
		  NewCapture(x, SPECIAL + 88 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // shoot 2nd
		  if(board[sqr-2*v] == EMPTY || (board[sqr-2*v] & TYPE) == xstm && board[sqr-2*v] > board[sqr])
		    NewCapture(x, SPECIAL + 80 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p. 1st and 2nd
		}
	      }
	      break;
	    case J: // plain jump (as in KY, PH)
	      if(d != 2) break;
	    case I: // jump + step (as in Wa TF)
	      if(d > 2) break;
	      NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	      att -= ray[i];
	      break;
	    case W: // jump + locust jump + 3-slide (Werewolf)
	      if(d > 2) break;
	      NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	      att -= ray[i];
	      if(d == 2) { // check if we can take intermediate with it
		if((board[x-v] & TYPE) == xstm && board[x-v] > board[sqr])
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // e.p.
	      } else { // d=1; can move on to second
		if(board[sqr-v] == EMPTY || (board[sqr-v] & TYPE) == xstm && board[sqr-v] > board[sqr])
		  NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag); // hit and run
	      }
	      break;
	    case C: // FIDE Pawn
	      if(d != 1) break;
	      NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag);
	      att -= ray[i];
	  }
	}
//printf("mask[%d] = %o\n", i, att);
	if((att & attackMask[i]) == 0) break;
      }
      // more attacks to come; scan for next stop
      if(jcapt < p[board[x]].qval) jcapt = p[board[x]].qval; // raise barrier for range jumpers further upstream
      while(board[x+=v] == EMPTY); // this should never run off-board, if the attack map is not corrupted
      }
    }
  }
  // off-ray attacks
  if(att & 0700000000) { // Knight attack
    for(i=0; i<RAYS; i++) {    // scan all knight jumps to locate source
      int x = sqr - nStep[i], attacker = board[x];
      if(attacker == EMPTY || (attacker & TYPE) != stm) continue;
      if(p[attacker].range[i] == L || p[attacker].range[i] < W && p[attacker].range[i] >= S || p[attacker].range[i] == N) { // has Knight jump in our direction
	NewCapture(x, sqr + victimValue, p[attacker].promoFlag);   // plain jump (as in N)
	if(p[attacker].range[i] < N) { // Lion power; generate double captures over two possible intermediates
	  int v = kStep[i]; // leftish path
	  if((board[x+v] & TYPE) == xstm && board[x+v] > board[sqr])
	    NewCapture(x, SPECIAL + RAYS*i + (i+1&7) + victimValue, p[attacker].promoFlag);
	  v = kStep[i+1];  // rightish path
	  if((board[x+v] & TYPE) == xstm && board[x+v] > board[sqr])
	    NewCapture(x, SPECIAL + RAYS*(i+1&7) + i + victimValue, p[attacker].promoFlag);
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
  return (dist(lion, king) - 23)*q;      // reduce by ~half if Lion very far away
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
  int wLion=ABSENT, bLion=ABSENT, score=mobilityScore, f;
#ifdef KINGSAFETY
  int wKing, bKing, i, j, max=512;
#endif

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
      score -= ( (1+9*!ATTACK(wLion, WHITE)) * lionTrap[UR-wLion]
               - (1+9*!ATTACK(bLion, BLACK)) * lionTrap[bLion] ) * distFac[dist(wLion, bLion)];
  }

# ifdef WINGS
  // bonus if corner lances are protected by Lion-proof setup (FL + C/S)
  if(bLion != ABSENT) {
    if((p[board[BW+LR]].value == 320 || p[board[BW+LR]].value == 220) &&
        p[board[LL+1]].value == 150 && p[board[LL + STEP(1, 2)]].value == 100) score += 20 + 20*!p[board[LL]].range[2];
    if((p[board[BW+LR]].value == 320 || p[board[BW+LR]].value == 220) &&
        p[board[LR-1]].value == 150 && p[board[LR + STEP(1, -2)]].value == 100) score += 20 + 20*!p[board[LR]].range[2];
  }
  if(wLion != ABSENT) {
    if((p[board[UL-BW]].value == 320 || p[board[UL-BW]].value == 220) &&
        p[board[UL+1]].value == 150 && p[board[UL-BW+2]].value == 100) score -= 20 + 20*!p[board[UL]].range[2];
    if((p[board[UR-BW]].value == 320 || p[board[UR-BW]].value == 220) &&
        p[board[UR-1]].value == 150 && p[board[UR-BW-2]].value == 100) score -= 20 + 20*!p[board[UR]].range[2];
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

#ifdef FORTRESS
  f = 0;
  if(bLion != ABSENT) f += Fortress( BW, wKing, bLion);
  if(wLion != ABSENT) f -= Fortress(-BW, bKing, wLion);
  score += (filling < 192 ? f : f*(224 - filling) >> 5); // build up slowly
#endif

#ifdef KSHIELD
  if(wKing && bKing) score += Surround(WHITE, wKing, 1, max) - Surround(BLACK, bKing, 1, max) >> 3;
#endif
#endif

#if KYLIN
  // bonus for having Kylin in end-game, where it could promote to Lion
  // depends on board population, defenders around zone entry and proximity to zone
  if(filling < 128) {
    int sq;
    if((wLion = kylin[WHITE]) && (sq = p[wLion].pos) != ABSENT) {
      int anchor = sq - PST[5*BSIZE - 1 - sq]; // FIXME: PST_ZONDIST indexed backwards
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
  for(i=pieces[WHITE]; i > 1 && p[i].value<=50; i-=2) {
    if((f = p[i].pos) != ABSENT) { // P present,
      if((j = board[f + BW])&1) // square before it white (odd) piece
	score -= 10 + 10*(p[j].promoGain > 0) + 30*!(p[j].range[3] || p[j].range[5] || p[j].value==50);
      if((j = board[f - BW])&1) // square behind it white (odd) piece
	score += 7*(p[j].promoGain == 0 & p[j].value<=151);
    }
  }
  for(i=pieces[BLACK]; i > 1 && p[i].value<=50; i-=2) {
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
      int rw = BW*(bRanks-1-zone), rb = BW*zone, h=0;
      for(f=0; f<bRanks; f++) {
	if(p[board[rw+f]].pst == PST_ADVANCE) {
	  h += (p[board[rw+f-BW]].pst == PST_ADVANCE);
	  if(f > 0)    h += (p[board[rw+f-BW-1]].pst == PST_ADVANCE);
	  if(f+1 < bFiles) h += (p[board[rw+f-BW+1]].pst == PST_ADVANCE);
	}
	if(p[board[rb+f]].pst == PST_ADVANCE) {
	  h -= (p[board[rb+f+BW]].pst == PST_RETRACT);
	  if(f > 0)    h -= (p[board[rb+f+BW-1]].pst == PST_RETRACT);
	  if(f+1 < bFiles) h -= (p[board[rb+f+BW+1]].pst == PST_RETRACT);
	}
      }
      score += h*TANDEM;
    }
#endif

  return difEval - (filling*promoDelta >> 8) + (stm ? score : -score);
}

static inline void
FireSet (UndoInfo *tb)
{ // set fireFlags acording to remaining presene of Fire Demons
  int i;
  for(i=stm+2; p[i].value == FVAL; i++) // Fire Demons are always leading pieces in list
    if(p[i].pos != ABSENT) tb->fireMask |= fireFlags[i-2];
}

void TerminationCheck();

int
Search (int alpha, int beta, int difEval, int depth, int lmr, int oldPromo, int promoSuppress, int threshold)
{
  int i, j, k, phase, king, nextVictim, defer, autoFail=0, inCheck=0, late=100000, ep;
  int firstMove, oldMSP=msp, curMove, sorted, bestMoveNr=0;
  int resDep=0, iterDep, ext;
  int myPV=pvPtr;
  int score, bestScore=0, oldBest, curEval, iterAlpha;
  Move move, nullMove=ABSENT;
  UndoInfo tb;
#ifdef HASH
  Move hashMove; int index, nr, hit;
#endif
/*if(PATH) pboard(board),pmap(BLACK);*/
#if 0
printf("\n# search(%d) {%d,%d} eval=%d stm=%d ",level,alpha,beta,difEval,stm);
#endif
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
      if(!ATTACK(k, xstm)) {
	if(tsume && tsume & stm+1) {
	  retDep = 60; return INF; // we win when not in check
        }
      }
#ifdef CHECKEXT
      else { inCheck = 1; if(depth >= QSDEPTH) depth++; }
#endif
    }
  }

if(!level) {for(i=0; i<5; i++)printf("# %d %08x, %d\n", i, repStack[LEVELS-i], checkStack[LEVELS-i]);}
  // KING CAPTURE
  k = p[king=royal[xstm]].pos;
  if(k != ABSENT) {
    if(ATTACK(k, stm) && p[king + 2].pos == ABSENT) { // we have an attack on his only King
      return INF;
    }
  } else { // he has no king! Test for attacks on Crown Prince
    k = p[king + 2].pos;
    if(k == ABSENT ? !tsume : ATTACK(k, stm)) { // we have attack on Crown Prince
      return INF;
    }
  }
  // EVALUATION & WINDOW SHIFT
  curEval = Evaluate(difEval) -20*inCheck;
  alpha -= (alpha < curEval);
  beta  -= (beta <= curEval);

  if(!(nodes++ & 4095)) TerminationCheck();
  pv[pvPtr++] = 0; // start empty PV, directly behind PV of parent
  if(inCheck) lmr = 0; else depth -= lmr; // no LMR of checking moves

  firstMove = j = curMove = sorted = msp; // leave 50 empty slots in front of move list
  iterDep = -(depth == 0); tb.fireMask = phase = 0;

#if 0
  printf("depth=%d iterDep=%d resDep=%d\n", depth, iterDep, resDep);
#endif
#ifdef HASH
  index = hashKeyL ^ 327*stm ^ (oldPromo + 987981)*(63121 + promoSuppress);
  index = index + (index >> 16) & hashMask;
  nr = (hashKeyL >> 30) & 3; hit = -1;
  if(hashTable[index].lock[nr] == hashKeyH) hit = nr; else
  if(hashTable[index].lock[4]  == hashKeyH) hit = 4;
#if 0
printf("# probe hash index=%x hit=%d\n", index, hit);
#endif
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
    hashMove = INVALID;
  }
#if 0
printf("# iterDep = %d score = %d hash move = %s\n",iterDep,bestScore,MoveToText(hashMove,0));
#endif
#endif

  if(depth > QSDEPTH) iterDep = MAX(iterDep, QSDEPTH); // full-width: start at least from 1-ply
  while(++iterDep <= depth) {
#if 0
if(depth >= QSDEPTH) printf("# new iter %d:%d\n", depth, iterDep);
#endif
    oldBest = bestScore;
    iterAlpha = alpha; bestScore = -INF; bestMoveNr = 0; resDep = 60;
    if(depth <= QSDEPTH) {
      bestScore = curEval; resDep = QSDEPTH;
      if(bestScore > alpha) {
	alpha = bestScore;
#if 0
printf("# stand pat %d (beta=%d)\n", bestScore, beta);
#endif
	if(bestScore >= beta) goto cutoff;
      }
    }
    for(curMove = firstMove; ; curMove++) { // loop over moves
if(PATH) printf("# phase=%d: first=%d curr=%d last=%d depth=%d:%d (%d)\n", phase, firstMove, curMove, msp, iterDep, depth, resDep);
      // MOVE SOURCE
      if(curMove >= msp) { // we ran out of moves; generate some new
if(PATH) printf("# new moves, phase=%d\n", phase);
	switch(phase) {
	  case 0: // null move
#ifdef NULLMOVE
	    if(depth > QSDEPTH && curEval >= beta && !inCheck && filling > 10) {
              int nullDep = depth - 3;
	      stm ^= WHITE;
path[level++] = 0;
if(PATH) printf("%d:%d null move\n", level, depth);
	      int score = -Search(-beta, 1-beta, -difEval, nullDep<QSDEPTH ? QSDEPTH : nullDep, 0, promoSuppress & SQUARE, ABSENT, INF);
if(PATH) printf("%d:%d null move score = %d\n", level, depth, score);
level--;
	      xstm = stm; stm ^= WHITE;
	      if(score >= beta) { msp = oldMSP; retDep += 3; pvPtr = myPV; return score + (score < curEval); }
//	      else depth += lmr, lmr = 0;
	    }
#endif
	    if(tenFlag) FireSet(&tb); // in tenjiku we must identify opposing Fire Demons to perform any moves
if(PATH && tenFlag) printf("fireMask=%x\n",tb.fireMask),pbytes(fireBoard);
	    phase = 1;
	  case 1: // hash move
	    phase = 2;
#ifdef HASH
	    if(hashMove && (depth > QSDEPTH || // must be capture in QS
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
	    while(nextVictim < pieces[xstm]) {        // more victims may exist
	      int group, to = p[nextVictim += 2].pos; // take next
	      if(to == ABSENT) continue;              // ignore if absent
	      if(!ATTACK(to, stm)) continue;          // skip if not attacked
	      group = p[nextVictim].value;            // remember value of this found victim
	      if(iterDep <= QSDEPTH + 1 && 2*group + curEval + 30 < alpha) {
		resDep = QSDEPTH + 1; nextVictim -= 2;
		if(bestScore < 2*group + curEval + 30) bestScore = 2*group + curEval + 30;
		goto cutoff;
	      }
if(PATH) printf("%d:%2d:%2d group=%d, to=%c%d\n", level, depth, iterDep, group, FILECH(to), RANK(to));
	      GenCapts(to, 0);
if(PATH) printf("%d:%2d:%2d first=%d last=%d\n",level,depth,iterDep,firstMove,msp);
	      while(nextVictim < pieces[xstm] && p[nextVictim+2].value == group) { // more victims of same value exist
		to = p[nextVictim += 2].pos;   // take next
		if(to == ABSENT) continue;     // ignore if absent
		if(!ATTACK(to, stm)) continue; // skip if not attacked
if(PATH) printf("%d:%2d:%2d p=%d, to=%c%d\n", level, depth, iterDep, nextVictim, FILECH(to), RANK(to)), fflush(stdout);
		GenCapts(to, 0);
if(PATH) printf("%d:%2d:%2d last=%d\n",level,depth,iterDep,msp);
	      }
if(PATH) printf("# captures on %c%d generated, last=%d, group=%d, threshold=%d\n", FILECH(to), RANK(to), msp, group, threshold);
	      goto extractMove; // in auto-fail phase, only search if they might auto-fail-hi
	    }
if(PATH) printf("# phase=%d autofail=%d\n", phase, autoFail);
	    if(autoFail) { // non-captures cannot auto-fail; flush queued captures first
if(PATH) printf("# phase=%d autofail end (%d-%d)\n", phase, firstMove, msp);
	      autoFail = 0; curMove = firstMove - 1; continue; // release stashed moves for search (next phase)
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
	    if(depth <= QSDEPTH) { if(resDep > QSDEPTH) resDep = QSDEPTH; goto cutoff; }
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

      // MOVE EXTRACTION (FROM GENERATED MOVES)
    extractMove:
if(PATH) printf("# extract sorted=%d\n", sorted);
      if(curMove > sorted) {
	move = moveStack[sorted=j=curMove];
	for(i=curMove+1; i<msp; i++)
	  if(move == INVALID || moveStack[i] > move) move = moveStack[j=i]; // search move with highest priority
	if(j>curMove) { moveStack[j] = moveStack[curMove]; moveStack[curMove] = move; } // swap highest-priority move to front of remaining
	if(move == INVALID) { msp = curMove--; continue; } // remaining moves are invalid; clip off move list
      } else {
	move = moveStack[curMove];
	if(move == INVALID) continue; // skip invalidated move
      }
#if 0
if(depth >= 0) printf("# (%d) evaluate 0x%04X %s level=%d autofail=%d\n", curMove, moveStack[curMove], MoveToText(moveStack[curMove], 0), level, autoFail);
#endif

      // RECURSION
      stm ^= WHITE;
      defer = MakeMove(move, &tb);
      ext = (depth == 0); // when out of depth we extend captures if there was no auto-fail-hi

//      if(level == 1 && randomize) tb.booty += (hashKeyH * seed >> 24 & 31) - 20;

      if(autoFail) {
	UnMake(&tb); // never search moves during auto-fail phase
	xstm = stm; stm ^= WHITE;
#if 0
printf("#       prune %d-%d ?= %d\n", tb.gain, tb.loss, threshold);
#endif
	if(tb.gain <= threshold) { // found refutations that cannot possibly auto-fail
	  autoFail = 0; curMove = firstMove-1; continue; // release all for search (next phase)
	}
        if(tb.gain - tb.loss > threshold) {
          bestScore = INF+1; resDep = 0; goto leave; // auto-fail-hi
        } else continue; // ignore for now if not obvious refutation
      }

#if 0
printf("#       validate 0x%04X %s\n", moveStack[curMove], MoveToText(moveStack[curMove], 0));
#endif
      for(i=2; i<=cnt50; i+=2) if(repStack[LEVELS+level-i] == hashKeyH) {
#if 0
printf("#       repetition %d\n", i);
#endif
	retDep = iterDep;
	if(repDraws) { score = 0; goto repetition; }
	if(!allowRep) {
	  moveStack[curMove] = INVALID; // erase forbidden repetition move
	  if(!level) repeatMove[repCnt++] = move & 0xFFFFFF; // remember outlawed move
	} else { // check for perpetuals
//	  int repKey = 1;
//	  for(i-=level; i>1; i-=2) {repKey &= checkStack[LEVELS-i]; if(!level)printf("# repkey[%d] = %d\n", LEVELS-i, repKey);}
	  if(inCheck) { score = INF-20; goto repetition; } // we might be subject to perpetual check: score as win
	  if(i == 2 && repStack[LEVELS+level-1] == hashKeyH) { score = INF-20; goto repetition; } // consecutive passing
	}
	score = -INF + 8*allowRep; goto repetition;
      }
      repStack[level+LEVELS] = hashKeyH;

path[level++] = move;
MapAttacks(); // for as long as incremental update does not work.
//if(PATH) pmap(stm);
      if(chuFlag && (p[tb.victim].value == LVAL || p[tb.epVictim[0]].value == LVAL)) {// verify legality of Lion capture in Chu Shogi
#if 0
printf("#       revalidate %d 0x%04X %s\n", level, moveStack[curMove], MoveToText(moveStack[curMove], 0));
#endif
	score = 0;
	if(p[tb.piece].value == LVAL) {          // Ln x Ln: can make Ln 'vulnerable' (if distant and not through intemediate > GB)
	  if(dist(tb.from, tb.to) != 1 && ATTACK(tb.to, stm) && p[tb.epVictim[0]].value <= 50)
	    score = -INF;                           // our Lion is indeed made vulnerable and can be recaptured
	} else {                                    // other x Ln
	  if(promoSuppress & PROMOTE) {             // non-Lion captures Lion after opponent did same
	    if(!okazaki || ATTACK(tb.to, stm)) score = -INF;
	  }
	  defer |= PROMOTE;                         // if we started, flag  he cannot do it in reply
	}
        if(score == -INF) {
          if(level == 1) repeatMove[repCnt++] = move & 0xFFFFFF | (p[tb.piece].value == LVAL ? 3<<24 : 1 << 24);
          moveStack[curMove] = INVALID; // zap illegal lion moves
          goto abortMove;
        }
      }
#if 1 // HGM
      score = -Search(-beta, -iterAlpha, -difEval - tb.booty, iterDep-1+ext,
                       curMove >= late && iterDep > QSDEPTH + lmr,
                                                      promoSuppress & ~PROMOTE, defer, depth ? INF : tb.gain);
#else
      score = 0;
#endif
    abortMove:
level--;
    repetition:
      UnMake(&tb);
      xstm = stm; stm ^= WHITE;
      if(abortFlag > 0) { // unwind search
printf("# abort (%d) @ %d\n", abortFlag, level);
        if(curMove == firstMove) bestScore = oldBest, bestMoveNr = firstMove; // none searched yet
        goto leave;
      }
if(PATH) printf("%d:%2d:%d %3d %6x %-10s %6d %6d (%d)\n", level, depth, iterDep, curMove, moveStack[curMove], MoveToText(moveStack[curMove], 0), score, bestScore, GetTickCount());

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
    } // next move
  cutoff:
    if(!level) { // root node
      lastRootIter = GetTickCount() - startTime;
      if(postThinking > 0) {
        int i;   // WB thinking output
	printf("%d %d %d %d", iterDep-QSDEPTH, bestScore, lastRootIter/10, nodes);
        if(ponderMove) printf(" (%s)", MoveToText(ponderMove, 0));
	for(i=0; pv[i]; i++) printf(" %s", MoveToText(pv[i], 0));
        if(iterDep == QSDEPTH+1) printf(" { root eval = %4.2f dif = %4.2f; abs = %4.2f f=%d D=%4.2f %d/%d}", curEval/100., difEval/100., PSTest()/100., filling, promoDelta/100., Ftest(0), Ftest(1));
	printf("\n");
      }
      if(!(abortFlag & 1) && GetTickCount() - startTime > tlim1) break; // do not start iteration we can (most likely) not finish
    }
#if 0
    printf("# (%d) %d CUT %d %d %d MAX(%d) %d\n", curMove, phase, depth, iterDep, resDep, MAX(iterDep, resDep), level);
#endif
    iterDep = MAX(iterDep, resDep); // skip iterations if we got them for free
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
  retFirst = firstMove;
  retMSP = msp;
  msp = oldMSP; // pop move list
  pvPtr = myPV; // pop PV
  retMove = bestMoveNr ? moveStack[bestMoveNr] : INVALID;
  retDep = resDep - (inCheck & depth >= QSDEPTH) + lmr;
#if 0
printf("#       %d %s (t=%d s=%d lim=%d)\n", bestScore, MoveToText(retMove, 0), GetTickCount(), startTime, tlim1);
#endif
  return bestScore + (bestScore < curEval);
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
pbytes (unsigned char *b)
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

    // some parameter of your engine
    #define MAXMOVES 2000 /* maximum game length  */
    #define MAXPLY   60   /* maximum search depth */

    #define OFF 0
    #define ON  1

    int moveNr;              // part of game state; incremented by MakeMove
    Move gameMove[MAXMOVES]; // holds the game history

    // Some routines your engine should have to do the various essential things
    int  MakeMove2(int stm, Move move);      // performs move, and returns new side to move
    void UnMake2(Move move);                 // unmakes the move;
    int  Setup2(char *fen);                  // sets up the position from the given FEN, and returns the new side to move
    void SetMemorySize(int n);              // if n is different from last time, resize all tables to make memory usage below n MB
    char *MoveToText(Move move, int m);     // converts the move from your internal format to text like e2e2, e1g1, a7a8q.
    Move ParseMove(int ls, int le, char *moveText); // converts a long-algebraic text move to your internal move format
    int  SearchBestMove(Move *move, Move *ponderMove);
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
    MapAttacks();
    if(ATTACK(k, WHITE-stm)) return 1;
  }
  return 0;
}

int
MakeMove2 (int stm, Move move)
{
  int i, inCheck = InCheck();
  FireSet(&undoInfo);
  sup0 = sup1; sup1 = sup2;
  sup2 = MakeMove(move, &undoInfo);
  if(chuFlag && p[undoInfo.victim].value == LVAL && p[undoInfo.piece].value != LVAL) sup2 |= PROMOTE;
  rootEval = -rootEval - undoInfo.booty;
  for(i=0; i<LEVELS; i++)
    repStack[i] = repStack[i+1], checkStack[i] = checkStack[i+1];
  repStack[LEVELS-1] = hashKeyH, checkStack[LEVELS-1] = inCheck;
  // makemove is not part of http://hgm.nubati.net/CECP.html
  printf("# makemove %s %c%d %c%d\n", MoveToText(move, 0), FILECH(sup1), RANK(sup1), FILECH(sup2), RANK(sup2));
  return stm ^ WHITE;
}

void
UnMake2 (Move move)
{
  int i;
  rootEval = -rootEval - undoInfo.booty;
  UnMake(&undoInfo);
  for(i=LEVELS; i>0; i--) repStack[i] = repStack[i-1], checkStack[i] = checkStack[i-1];
  sup2 = sup1; sup1 = sup0;
}

int
Setup2 (char *fen)
{
  char *p;
  int stm = WHITE;
  if(fen) {
    char *q = strchr(fen, '\n');
    if(q) *q = 0;
    if(q = strchr(fen, ' ')) stm = (q[1] == 'b' ? BLACK : WHITE); // fen contains color field
  } else fen = array;
  rootEval = promoDelta = filling = cnt50 = moveNr = 0;
  SetUp(fen, currentVariant);
  sup0 = sup1 = sup2 = ABSENT;
  hashKeyH = hashKeyL = 87620895*currentVariant + !!fen;
  for(p=startPos; *p++ = *fen++; ) {} // remember last start position for undo
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
MoveToText (Move move, int multiLine)
{
  static char buf[50];
  int from = move>>SQLEN & SQUARE, to = move & SQUARE;
  char *promoChar = "";
  if(from == to) { sprintf(buf, "@@@@"); return buf; } // null-move notation in WB protocol
  buf[0] = '\0';
  if(to >= SPECIAL) {
   if(to < CASTLE) { // castling is printed as a single move, implying its side effects
    int e = from + epList[to - SPECIAL];
    if(ep2List[to - SPECIAL]) {
      int e2 = from + ep2List[to - SPECIAL];
//      printf("take %c%d\n", FILECH(e), RANK(e));
      sprintf(buf+strlen(buf), "%c%d%c%d,", FILECH(from), RANK(from), FILECH(e2), RANK(e2)); from = e2;
      if(multiLine) printf("move %s\n", buf), buf[0] = '\0';
    }
//    printf("take %c%d\n", FILECH(e), RANK(e));
    sprintf(buf, "%c%d%c%d,", FILECH(from), RANK(from), FILECH(e), RANK(e)); from = e;
    if(multiLine) printf("move %s\n", buf), buf[0] = '\0';
   }
   to = (move>>SQLEN & SQUARE) + toList[to - SPECIAL];
  }
  if(move & PROMOTE) promoChar = currentVariant == V_MAKRUK ? "m" : currentVariant == V_WOLF ? "r" : repDraws ? "q" : "+";
  sprintf(buf+strlen(buf), "%c%d%c%d%s", FILECH(from), RANK(from), FILECH(to), RANK(to), promoChar);
  return buf;
}

int
ReadSquare (char *p, int *sqr)
{
  int f, r;
  f = p[0] - 'a';
  r = atoi(p+1) - 1;
  *sqr = POS(r, f);
  return 2 + (r + 1 > 9);
}

int
ListMoves (int listStart, int listEnd)
{ // create move list on move stack
  int i;
MapAttacks();
  postThinking--; repCnt = 0; tlim1 = tlim2 = tlim3 = 1e8; abortFlag = msp = 0;
  Search(-INF-1, INF+1, 0, QSDEPTH+1, 0, sup1 & ~PROMOTE, sup2, INF);
  postThinking++;

#if 0
printf("last=%d nc=%d retMSP=%d\n", msp, nonCapts, retMSP);
#endif
  msp = retMSP;
  if(currentVariant == V_LION) listEnd = GenCastlings();      // castlings for Lion Chess
  if(currentVariant == V_WOLF) for(i=listStart; i<msp; i++) { // mark Werewolf captures as promotions
    int to = moveStack[i] & SQUARE, from = moveStack[i] >> SQLEN & SQUARE;
    if(to >= SPECIAL) continue;
    if(p[board[to]].ranking >= 5 && p[board[from]].ranking < 4) moveStack[i] |= PROMOTE;
  }
  return listEnd;
}

Move
ParseMove (int listStart, int listEnd, char *moveText)
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
    for(i=0; i<RAYS; i++) if(f + kStep[i] == e) break;
    if(i >= RAYS) { // first leg not King step. Try Lion Dog 2+1 or 2-1
      for(i=0; i<RAYS; i++) if(f + 2*kStep[i] == e) break;
      if(i >= RAYS) return INVALID; // not even that
      if(f + 3*kStep[i] == t)    t2 = SPECIAL + 72 + i; // 2+1
      else if(f + kStep[i] == t) t2 = SPECIAL + 88 + i; // 2-1
      else return INVALID;
    } else if(f + 3*kStep[i] == t) { // Lion Dog 1+2 move
      t2 = SPECIAL + 64 + i;
    } else if(*moveText == ',') { // 3rd leg follows!
      moveText++;
      if(f + 2*kStep[i] != t) return INVALID; // 3-leg moves must be linear!
      moveText += ReadSquare(moveText, &e);
      if(e != t) return INVALID; // must again continue with same piece
      moveText += ReadSquare(moveText, &t);
      if(f + 3*kStep[i] == t)    t2 = SPECIAL + 80 + i; // 1+1+1
      else if(f + kStep[i] == t) t2 = SPECIAL + 88 + i; // 2-1 entered as 1+1-1
      else return INVALID;
    } else {
      for(j=0; j<RAYS; j++) if(e + kStep[j] == t) break;
      if(j >= RAYS) return INVALID; // this rejects Lion Dog 1+2 moves!
      t2 = SPECIAL + RAYS*i + j;
    }
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
  if(currentVariant == V_WOLF && *moveText == 'w') *moveText = '\n';
  if(*moveText != '\n' && *moveText != '=') ret |= PROMOTE;
printf("# suppress = %c%d\n", FILECH(sup1), RANK(sup1));
#if 1
  // TODO: do not rely upon global retMSP assignment (castlings for Lion Chess)
  retMSP = listEnd = ListMoves(listStart, listEnd);
#endif
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
	if(!(flags & promoBoard[f]) && currentVariant != V_WOLF) moveStack[i] |= DEFER; // came from outside zone, so essential deferral
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
Highlight (int listStart, int listEnd, char *coords)
{
  int boardCopy[BSIZE];
  memcpy(boardCopy, board, sizeof(board));

  int i, j, n, sqr, cnt=0;
  char b[BSIZE]={0}, buf[2000], *q;
  ReadSquare(coords, &sqr);
  for(i=listStart; i<listEnd; i++) {
    if(sqr == (moveStack[i]>>SQLEN & SQUARE)) {
      int t = moveStack[i] & SQUARE;
      if(t >= CASTLE) t = toList[t - SPECIAL]; else  // decode castling
      if(t >= SPECIAL) {
	int e = sqr + epList[t - SPECIAL]; // decode
	b[e] = 'C';
	continue;
      }
      if(!b[t]) {
        b[t] = (!boardCopy[t] ? 'Y' : 'R'); cnt++;
      }
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
	if(sqr == lastLift + ep2List[t - SPECIAL]) { // second leg of 3-leg move
	  b[e] = 'C'; cnt++;
	  continue;
	}
	t = lastLift + toList[t - SPECIAL];
	if(e != sqr) continue;
	if(!b[t]) {
          b[t] = (!boardCopy[t] ? 'Y' : 'R'); cnt++;
	}
      }
    }
    if(!cnt) return;
  } else lastLift = sqr; // remember
  lastPut = ABSENT;
  q = buf;
  for(i=bRanks-1; i>=0; i--) {
    for(j=0; j<bFiles; j++) {
      n = POS(i, j);
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
  int targetTime, movesLeft = bRanks*bFiles/4 + 20;
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
SearchBestMove (Move *move, Move *ponderMove)
{
  int score;
printf("# SearchBestMove\n");
  startTime = GetTickCount();
  nodes = 0;
printf("# s=%d\n", startTime);fflush(stdout);
MapAttacks();
  retMove = INVALID; repCnt = 0;
  score = Search(-INF-1, INF+1, rootEval, maxDepth + QSDEPTH, 0, sup1, sup2, INF);
  *move = retMove;
  *ponderMove = pv[1];
printf("# best=%s\n", MoveToText(pv[0],0));
if(pv[1]) printf("# ponder=%s\n", MoveToText(pv[1],0));
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
        do {
          for(i = 0; (inBuf[i] = c = getchar()) != '\n'; i++) if(c == EOF || i>7997) exit(0);
          inBuf[i+1] = 0;
        } while(!i); // ignore empty lines

        // extract the first word
        sscanf(inBuf, "%s", command);
printf("# in (mode = %d,%d): %s\n", root, abortFlag, command);
        if(!strcmp(command, "otim"))    { continue; } // do not start pondering after receiving time commands, as move will follow immediately
        if(!strcmp(command, "time"))    { sscanf(inBuf, "time %d", &timeLeft); continue; }
        if(!strcmp(command, "put"))     { ReadSquare(inBuf+4, &lastPut); continue; }  // ditto
        if(!strcmp(command, "."))       { inBuf[0] = 0; return; } // ignore for now
        if(!strcmp(command, "hover"))   { inBuf[0] = 0; return; } // ignore for now
        if(!strcmp(command, "lift"))    { inBuf[0] = 0; retMSP = ListMoves(retFirst, retMSP); Highlight(retFirst, retMSP, inBuf+5); return; } // treat here
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

    int
    main()
    {
      int engineSide=NONE;                // side played by engine
      Move move;
      int i, score, curVarNr = 0;

      setvbuf(stdin, NULL, _IOLBF, 1024); // buffering more than one line flaws test for pending input!

      Init(V_CHU); // Chu
      seed = startTime = GetTickCount(); moveNr = 0; // initialize random

      while(fflush(stdout) != EOF) { // infinite loop; make sure everything is printed before we do something that might take time
        *inBuf = 0; if(moveNr >= 20) randomize = OFF;
//if(moveNr >20) printf("resign\n");

#ifdef HASH
	if(hashMask)
#endif
        if(retMSP == 0) retMSP = ListMoves(retFirst, retMSP);   // always maintain a list of legal moves in root position
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
              if(ATTACK(k, stm)) {
                if( p[king + 2].pos == ABSENT ) kcapt = 1; // we have an attack on his only King
              }
            } else { // he has no king! Test for attacks on Crown Prince
              k = p[king + 2].pos;
              if(ATTACK(k, stm)) kcapt = 1; // we have attack on Crown Prince
            }
            if(kcapt) { // print King capture before claiming
              GenCapts(k, 0);
              printf("move %s\n", MoveToText(moveStack[msp-1], 1));
              reason = "king capture";
            } else reason = "resign";
            engineSide = NONE;          // so stop playing
            PrintResult(stm, score);
          } else {
            Move f, pMove = move;
            static char *pName[] = { "w", "z", "j" };
            if((move & SQUARE) >= SPECIAL && p[board[f = move>>SQLEN & SQUARE]].value == pVal) { // e.p. capture
              pMove = move & ~SQUARE | f + toList[(move & SQUARE) - SPECIAL]; // print as a single move
            }
            stm = MakeMove2(stm, move);  // assumes MakeMove returns new side to move
            gameMove[moveNr++] = move;   // remember game
            i = p[undoInfo.victim].ranking;
            printf("move %s%s\n", MoveToText(pMove, 1), i == 5 && p[undoInfo.piece].ranking < 4 ? pName[i-5] : "");
            retMSP = 0;                  // list has been printed
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
          if(sscanf(inBuf, "level %d %d %d", &mps, &min, &inc) != 3)  // if this does not work, it must be min:sec format
             sscanf(inBuf, "level %d %d:%d %d", &mps, &min, &sec, &inc);
          timeControl = 60*min + sec; timePerMove = -1;
          continue;
        }
        if(!strcmp(command, "protover")){
          for(i=0; variants[i].boardRanks; i++)
            printf("%s%s", (i ? "," : "feature variants=\""), variants[i].name);
          printf("\"\n");
          printf("feature ping=1 setboard=1 colors=0 usermove=1 memory=1 debug=1 sigint=0 sigterm=0\n");
          printf("feature myname=\"HaChu " VERSION "\" highlight=1\n");
          printf("feature option=\"Full analysis PV -check %d\"\n", noCut); // example of an engine-defined option
          printf("feature option=\"Allow repeats -check %d\"\n", allowRep);
          printf("feature option=\"Promote on entry -check %d\"\n", entryProm);
          printf("feature option=\"Okazaki rule -check %d\"\n", okazaki);
          printf("feature option=\"Resign -check %d\"\n", resign);
          printf("feature option=\"Contempt -spin %d -200 200\"\n", contemptFactor); // and another one
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
        if(!strcmp(command, "w"))       { MapAttacksByColor(WHITE, pieces[WHITE]); pmap(WHITE); continue; }
        if(!strcmp(command, "b"))       { MapAttacksByColor(BLACK, pieces[BLACK]); pmap(BLACK); continue; }
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
          int move = ParseMove(retFirst, retMSP, inBuf+9);
pboard(board);
          if(move == INVALID) {
            if(reason) printf("Illegal move {%s}\n", reason); else printf("%s\n", reason="Illegal move");
            if(comp) PrintResult(stm, -INF); // against computer: claim
          } else {
            stm = MakeMove2(stm, move);
            ponderMove = INVALID; retMSP = 0; // list has been consumed
            gameMove[moveNr++] = move;  // remember game
          }
          continue;
        }
        ponderMove = INVALID; // the following commands change the position, invalidating ponder move
        retMSP = 0;           // list has been consumed
        if(!strcmp(command, "new"))     {
          engineSide = BLACK; Init(V_CHESS); stm = Setup2(NULL); maxDepth = MAXPLY; randomize = OFF; curVarNr = comp = 0;
          continue;
        }
        if(!strcmp(command, "variant")) {
          for(i=0; variants[i].boardRanks; i++) {
            sscanf(inBuf+8, "%s", command);
            if(!strcmp(variants[i].name, command)) {
              Init(curVarNr = i); stm = Setup2(NULL); break;
            }
	  }
          if(currentVariant == V_WOLF)
            printf("setup (PNBR......................WKpnbr......................wk) 8x8+0_fairy rnbwkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBWKBNR w 0 1\n"
                   "piece W& K3cpafK\n");
          if(currentVariant == V_SHO)
            printf("setup (PNBRLSE..G.+++++++Kpnbrlse..g.+++++++k) 9x9+0_shogi lnsgkgsnl/1r2e2b1/ppppppppp/9/9/9/PPPPPPPPP/1B2E2R1/LNSGKGSNL w 0 1\n");
          if(currentVariant == V_WA)
            printf("setup (P..^S^FV..^LW^OH.F.^R.E....R...D.GOL^M..^H.M.C.^CU.^W/.......^V.^P.^U..^DS.^GXK"
                          "p..^s^fv..^lw^oh.f.^r.e....r...d.gol^m..^h.m.c.^cu.^w/.......^v.^p.^u..^ds.^gxk) 11x11+0_chu "
							"hmlcvkwgudo/1e3s3f1/ppprpppxppp/3p3p3/11/11/11/3P3P3/PPPXPPPRPPP/1F3S3E1/ODUGWKVCLMH w 0 1\n"
                   "piece P& fW\npiece O& fR\npiece H& fRbW2\npiece U& fWbF\npiece L& fWbF\npiece M& vWfF\npiece G& vWfF\npiece C& sWfF\n"
                   "piece +P& WfF\npiece +O& K\npiece +H& vN\npiece +U& BfW\npiece +L& vRfF3bFsW\npiece +M& FfW\npiece +G& sRvW\npiece +C& vRsWfF\n"
                   "piece D& sbWfF\npiece V& FfW\npiece W& WfF\npiece S& sRvW\npiece R& FfRbW\npiece F& BfW\npiece X& FvWAvD\n"
                   "piece +D& WfF\npiece +V& FfsW\npiece +W& K\npiece +S& R\npiece +R& FvWAvD\npiece +F& BvRsW\npiece E& vRfF3bFsW\n");
          if(currentVariant == V_MACAD)
            printf("setup (P.*B*RQSEXOG....D^J'..*LP'.L!J'=J...*W!...*F'...^C.C.^L!.^P'^K.T*L'.*C!*H!^I'=Z.^E...*R'^P^T*W'*G'^G^SI'^X^OK"
                          "p.*b*rqsexog....d^j'..*lp'.l!j'=j...*w!...*f'...^c.c.^l!.^p'^k.t*l'.*c!*h!^i'=z.^e...*r'^p^t*w'*g'^g^si'^x^ok) 13x13+0_chu %s w 0 1\n"
		   "piece P& fW\npiece S& FfW\npiece E& FfsW\npiece X& WA\npiece O& FD\npiece G& WfF\npiece D& RF\npiece +J'& QNADcaKmabK\n"
		   "piece L& fR\npiece P'& vW\npiece L!& KNADcaKmabK\npiece J'& blfFrW\npiece H!& RmasR\npiece W!& KADcavKmcpafmcpavK\n"
		   "piece C!& BmasB\npiece F'& F2\npiece +C& vRfB\npiece C& vWfF\npiece +L!& K3NADcaKmabK\npiece +P'& vR\npiece T& FbsW\n"
		   "piece L'& lfrbBfRbW\npiece +I'& QADcavKmcpafmcpavK\npiece +E& K\npiece R'& rflbBfRbW\npiece +P& WfF\npiece +T& BbsR\n"
		   "piece W'& F3sW\npiece G'& W3fF\npiece +G& RfB\npiece +S& BfR\npiece I'& rbfFlW\npiece +X& F3vRsW\npiece +O& F3sRvW\n", macadArray);
          if(currentVariant == V_CASHEW)
            printf("setup (P.^K'^P'QS^W!XOGND'.HDT!...P'.L!E'.^W'K'W!..LF'V^E'J'H'...^L!^N..FT'L'C'G!H!D!I'.^H'..R'..^C'^F'..W'^X^OK"
                          "p.^k'^p'qs^w!xognd'.hdt!...p'.l!e'.^w'k'w!..lf'v^e'j'h'...^l!^n..ft'l'c'g!h!d!i'.^h'..r'..^c'^f'..w'^x^ok) 13x13+0_chu %s w 0 1\n"
                   "piece P& fW\npiece S& FfW\npiece W'& fFvWsW2\npiece E'& fFsWvW2\npiece X& WA\npiece O& FD\npiece G& WfF\npiece H& BW\n"
                   "piece +C'& BW\npiece D& RF\npiece +F'& RF\npiece L& fR\npiece D'& fRbW\npiece L!& KNADcaKmabK\npiece +E'& KNADcaKmabK\n"
                   "piece J'& FvlW\npiece H!& RmasR\npiece +W'& KADcavKmcpafmcpavK\npiece G!& WBmasB\npiece F'& F2\npiece +L!& K3NADcaKmabK\n"
                   "piece T!& vRsW2flBfrF\npiece T'& fFvW2\npiece L'& lfrbBfRbW\npiece N& K\npiece R'& rflbBfRbW\npiece I'& FvrW\n"
                   "piece +X& F3vRsW2\npiece +O& F3sRvW2\npiece H'& WfF2\npiece +H'& Q\npiece C'& F\npiece D!& sRvW2frBflF\npiece P'& sWfDbA\n"
                   "piece K'& W2fF\npiece +K'& WBmasB\npiece +P'& RmasR\npiece +N& fRfBbF2bsW2\npiece F& FvW\npiece V& fF2sW\n", cashewArray);
	  repStack[LEVELS-1] = hashKeyH, checkStack[LEVELS-1] = 0;
          continue;
        }
        if(!strcmp(command, "setboard")){ engineSide = NONE;  Init(curVarNr); stm = Setup2(inBuf+9); continue; }
        if(!strcmp(command, "undo"))    { stm = TakeBack(1); continue; }
        if(!strcmp(command, "remove"))  { stm = TakeBack(2); continue; }
        printf("Error: unknown command\n");
      }
      return 0;
    }

