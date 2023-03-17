/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#include <stdlib.h>
#include "board.h"
#include "eval.h"
#include "piece.h"

#define LIONTRAP
#define KINGSAFETY
#define KSHIELD
#define FORTRESS
#define PAWNBLOCK
#define TANDEM 100 /* bonus for pairs of attacking light steppers */
#define PROMO 0 /* extra bonus for 'vertical' piece when it actually promotes (diagonal pieces get half) */

signed char psq[PSTSIZE][BSIZE] = { 0 }; // cache of piece-value-per-square

int hashKeyH=1, hashKeyL=1, rootEval, filling, promoDelta;
int mobilityScore;

int
Evaluate (Color c, int tsume, int difEval)
{
  int wLion=ABSENT, bLion=ABSENT, score=mobilityScore, f;
#ifdef KINGSAFETY
  int wKing, bKing, i, j, max=512;
#endif

  if(tsume) return difEval;

  if(LION(WHITE+2)) wLion = p[WHITE+2].pos;
  if(LION(BLACK+2)) bLion = p[BLACK+2].pos;
  if(wLion == ABSENT && LION(WHITE+4)) wLion = p[WHITE+4].pos;
  if(bLion == ABSENT && LION(BLACK+4)) bLion = p[BLACK+4].pos;

#ifdef LIONTRAP
  // penalty for Lion in enemy corner, when enemy Lion is nearby
  if(wLion != ABSENT && bLion != ABSENT) { // both have a Lion
      static int distFac[36] = { 0, 0, 10, 9, 8, 7, 5, 3, 1 };
      score -= ( (1+9*!ATTACK(wLion, WHITE)) * PSQ(PST_TRAP, wLion, WHITE)
               - (1+9*!ATTACK(bLion, BLACK)) * PSQ(PST_TRAP, bLion, BLACK) ) * distFac[dist(wLion, bLion)];
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
    int lead = (c == WHITE ? difEval : -difEval);
    score += (PSQ(PST_CENTER, wKing, WHITE) - PSQ(PST_CENTER, bKing, BLACK))*(32 - filling) >> 7;
    if(lead  > 100) score -= PSQ(PST_CENTER, bKing, BLACK)*(32 - filling) >> 3; // white leads, drive black K to corner
    if(lead < -100) score += PSQ(PST_CENTER, wKing, WHITE)*(32 - filling) >> 3; // black leads, drive white K to corner
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
      int anchor = sq - PSQ(PST_ZONDIST, sq, WHITE); // FIXME: PST_ZONDIST indexed backwards
      score += (512 - Surround(BLACK, anchor, 0, 512))*(128 - filling)*PSQ(p[wLion].pst, sq, WHITE) >> 15;
    }
    if((bLion = kylin[BLACK]) && (sq = p[bLion].pos) != ABSENT) {
      int anchor = sq + PSQ(PST_ZONDIST, sq, BLACK);
      score -= (512 - Surround(WHITE, anchor, 0, 512))*(128 - filling)*PSQ(p[bLion].pst, sq, BLACK) >> 15;
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

  return difEval - (filling*promoDelta >> 8) + (c ? score : -score);
}

int
Surround (Color c, int king, int start, int max)
{
  int i, s=0;
  for(i=start; i<9; i++) {
    int v, piece, sq = king + neighbors[i];
    if((piece = board[sq]) == EDGE || !piece || piece&1^c) continue;
    if(p[piece].promoGain) continue;
    v = p[piece].value;
    s += -(v > 70) & v;
  }
  return (s > max ? max : s);
}

int
Fortress (int forward, int king, int lion)
{ // penalty for lack of Lion-proof fortress
  int rank = PSQ(PST_NEUTRAL, king, BLACK), anchor, r, l, q, res = 0;
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
Ftest (int side)
{
  int lion = ABSENT, king;
  if(LION(side+2)) lion = p[side+2].pos;
  if(lion == ABSENT && LION(side+4)) lion = p[side+4].pos;
  king = p[royal[1-side]].pos; if(king == ABSENT) king = p[royal[1-side]+1].pos;
  return lion == ABSENT ? 0 : Fortress(side ? -BW : BW, king, lion);
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
