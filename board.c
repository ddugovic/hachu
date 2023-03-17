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

#define KYLIN 100 /* extra end-game value of Kylin for promotability */

VariantDesc *variant;
int bFiles, bRanks, zone, currentVariant, repDraws, stalemate;

int pVal;
int hashKeyH=1, hashKeyL=1, framePtr, rootEval, filling, promoDelta;
int cnt50;

int board[BSIZE] = { [0 ... BSIZE-1] = EDGE };

int attacksByLevel[LEVELS][COLORS][BSIZE];
int multis[COLORS], multiMovers[NPIECES];

Flag fireBoard[BSIZE]; // flags to indicate squares controlled by Fire Demons
Flag fireFlags[10]; // flags for Fire-Demon presence (last two are dummies, which stay 0, for compactify)

Flag
IsEmpty (int sqr)
{
  return board[sqr] == EMPTY;
}

void
SetUp (char *fen, char *IDs, int var)
{
  int i, j, n, m, color;
  char name[3], prince = 0;
  pieces[WHITE] = WHITE; pieces[BLACK] = BLACK;
  royal[WHITE] = royal[BLACK] = 0;
  for(i=bRanks-1; ; i--) {
//printf("next rank: %s\n", fen);
    for(j = bFiles*i; ; j++) {
      int pflag=0;
      if(*fen == '+') pflag++, fen++;
      int ch = name[0] = *fen++;
      if(!ch) goto eos;
      if(ch == '.') continue;
      if(ch > '0' && ch <= '9') {
        ch -= '0'; if(*fen >= '0' && *fen <= '9') ch = 10*ch + *fen++ - '0';
        j += ch - 1; continue;
      }
      if(ch == '/') break;
      name[1] = name[2] = 0;
      if(ch >= 'a') {
	color = BLACK;
	ch += 'A' - 'a';
      } else color = WHITE;
      if(*fen == '\'') ch += 26, fen++; else
      if(*fen == '!')  ch += 52, fen++;
      name[0] = IDs[2*(ch - 'A')];
      name[1] = IDs[2*(ch - 'A') + 1]; if(name[1] == ' ') name[1] = 0;
      if(!strcmp(name, "CP") || pflag && !strcmp(name, "DE")) prince |= color+1; // remember if we added Crown Prince
      PieceDesc *p1 = LookUp(name, var);
      if(!p1) printf("tellusererror Unknown piece '%s' in setup (%d)\n", name, ch), exit(-1);
      if(pflag && p1->promoted) p1 = LookUp(p1->promoted, var); // use promoted piece instead
      n = AddPiece(color, p1);
      p[n].pos = POS(j / bFiles, j % bFiles);
      if(p1->promoted[0] && !pflag) {
        if(!strcmp(p1->promoted, "CP")) prince |= color+1; // remember if we added Crown Prince
        PieceDesc *p2 = LookUp(p1->promoted, var);
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
//    if(j > 0 && p[i].pst == PST_STEPPER) p[i].pst = PST_PPROM; // use white pre-prom bonus
    if(j > 0 && p[i].pst == PST_STEPPER && p[i].value >= 100)
	p[i].pst = p[i].value <= 150 ? PST_ADVANCE : PST_PPROM;  // light steppers advance
    if(j > 0 && p[i].bulk == 6) p[i].pst = PST_FLYER, p[i].mobWeight = 4; // SM defends zone
    if((j = p[i].promo) > 0 && g)
      p[i].promoGain = (p[j].value - p[i].value - g)*0.9, p[i].value = p[j].value - g;
    else p[i].promoGain = 0;
    board[p[i].pos] = i;
    rootEval += p[i].value + PSQ(p[i].pst, p[i].pos, WHITE);
    promoDelta += p[i].promoGain;
    filling += p[i].bulk;
  } else p[i].promoGain = 0;
  for(i=BLACK+2; i<=pieces[BLACK]; i+=2) if(p[i].pos != ABSENT) {
    int g = p[i].promoGain;
//    if(j > 0 && p[i].pst == PST_STEPPER) p[i].pst = PST_PPROM; // use black pre-prom bonus
    if(j > 0 && p[i].pst == PST_STEPPER && p[i].value >= 100)
	p[i].pst = p[i].value <= 150 ? PST_RETRACT : PST_PPROM;  // light steppers advance
    if(j > 0 && p[i].bulk == 6) p[i].pst = PST_FLYER, p[i].mobWeight = 4; // SM defends zone
    if((j = p[i].promo) > 0 && g)
      p[i].promoGain = (p[j].value - p[i].value - g)*0.9, p[i].value = p[j].value - g;
    else p[i].promoGain = 0;
    if(i == kylin[BLACK]) p[i].promoGain = 1.25*KYLIN, p[i].value += KYLIN;
    board[p[i].pos] = i;
    rootEval -= p[i].value + PSQ(p[i].pst, p[i].pos, BLACK);
    promoDelta -= p[i].promoGain;
    filling += p[i].bulk;
  } else p[i].promoGain = 0;
  StackMultis(WHITE);
  StackMultis(BLACK);
}

void
StackMultis (Color c)
{
  int i, j;
  multis[c] = c;
  for(i=c+COLORS; i<=pieces[c]; i+=COLORS) { // scan piece list for multi-capturers
    for(j=0; j<RAYS; j++) if(p[i].range[j] < J && p[i].range[j] >= S || p[i].value == FVAL) {
      multiMovers[multis[c]] = i;  // found one: put its piece number in list
      multis[c] += COLORS;
      break;
    }
  }
}

int
PSTest ()
{
  int r, f, score, tot=0;
  for(r=0; r<bRanks; r++) for(f=0; f<bFiles; f++) {
    int s = POS(r, f);
    int piece = board[s];
    if(!piece) continue;
    score = p[piece].value + PSQ(p[piece].pst, s, BLACK);
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
MapAttacksByColor (Color color, int pieces, int level)
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
  return totMob;
}

int
MapAttacks (int level)
{
  int blackMob = MapAttacksByColor(BLACK, pieces[BLACK], level),
      whiteMob = MapAttacksByColor(WHITE, pieces[WHITE], level);
//if(!level) printf("# mobility WHITE = %d, BLACK = %d\n", whiteMob, blackMob);
  return whiteMob - blackMob;
}

int
MakeMove (Color stm, Move m, UndoInfo *u)
{
  int deferred = ABSENT;
  // first execute move on board
  u->from = FROM(m);
  u->to = m & SQUARE;
  u->piece = board[u->from];
  board[u->from] = EMPTY;
  u->booty = 0;
  u->gain  = 0;
  u->loss  = 0;
  u->revMoveCount = cnt50++;
  u->savKeyL = hashKeyL;
  u->savKeyH = hashKeyH;
  memset(u->epVictim, EMPTY, (RAYS+1)*sizeof(int));
  u->saveDelta = promoDelta;
  u->filling = filling;

  if(p[u->piece].promoFlag & LAST_RANK) cnt50 = 0; // forward piece: move is irreversible
  // TODO: put in some test for forward moves of non-backward pieces?
//		int n = board[promoSuppress-1];
//		if( n != EMPTY && (n&TYPE) == INVERT(stm) && p[n].value == 8 ) NewNonCapt(promoSuppress-1, 16, 0);

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
    u->booty += p[u->epVictim[1]].value + PSQ(p[u->epVictim[1]].pst, u->ep2Square, BLACK);
    u->booty += p[u->epVictim[0]].value + PSQ(p[u->epVictim[0]].pst, u->epSquare, BLACK);
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
	if(burnVictim != EMPTY && (burnVictim & TYPE) == INVERT(stm)) { // opponent => actual burn
	  board[x] = EMPTY; // remove it
	  p[burnVictim].pos = ABSENT;
	  u->booty += p[burnVictim].value + PSQ(p[burnVictim].pst, x, BLACK);
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

  u->booty += PSQ(p[u->new].pst, u->to, BLACK) - PSQ(p[u->piece].pst, u->from, BLACK);

  filling += p[u->new].bulk - p[u->piece].bulk - p[u->victim].bulk;
  promoDelta += p[u->new].promoGain - p[u->piece].promoGain + p[u->victim].promoGain;
  u->booty += p[u->victim].value + PSQ(p[u->victim].pst, u->to, BLACK);
  u->gain  += p[u->victim].value;
  if(u->victim != EMPTY) {
    cnt50 = 0; // capture irreversible
    if(ATTACK(u->to, INVERT(stm))) u->loss = p[u->piece].value; // protected
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
UnMake (UndoInfo *u)
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
pboard (int *b)
{
  int i, j, p;
  for(i=0; i<bRanks; i++) {
    printf("#");
    for(j=0; j<bFiles; j++) {
      p=b[POS(i, bFiles-j-1)];
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
pmap (Color c)
{
  // decode out of double-wide "attacks" array
  int i, j;
  for(i=bFiles-1; i>=0; i--) {
    printf("#");
    for(j=0; j<bRanks; j++) printf("%11o", ATTACK(POS(i, j), c));
    printf("\n");
  }
}
