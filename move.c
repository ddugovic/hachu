/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hachu.h" // TODO: reduce dependency on ListMoves/Search
#include "move.h"
#include "piece.h"
#include "types.h"
#include "variant.h"

int sup0, sup1, sup2; // promo suppression squares
int repCnt;
char *reason;

MoveInfo
MoveToInfo (Move move)
{
  MoveInfo info = { .from = FROM(move), .to = move & SQUARE, .path = { EDGE, EDGE } };
  if(info.to >= SPECIAL) {
    int i = info.to - SPECIAL;
    if(info.to < CASTLE) {
      if(ep2List[i]) {
        info.path[1] = info.from + ep2List[i];
      }
      info.path[0] = info.from + epList[i];
    }
    // decode (ray-based) lion move or castling move
    info.to = info.from + toList[i];
  }
  return info;
}

char *
MoveToText (Move move, int multiLine) // copied from WB driver
{
  static char buf[50];
  int from = FROM(move), to = move & SQUARE;
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
   to = FROM(move) + toList[to - SPECIAL];
  }
  if(move & PROMOTE) promoChar = makrukFlag ? "m" : wolfFlag ? "r" : repDraws ? "q" : "+";
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

Move
ParseMove (Color stm, int listStart, int listEnd, char *moveText, Move *moveStack, Move *repeatMove, int *retMSP)
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
      t2 = SPECIAL + RAY(i, j);
    }
  } else if(chessFlag && board[f] != EMPTY && p[board[f]].value == pVal && IsEmpty(t)) { // Pawn to empty, could be e.p.
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
  // TODO: do not rely upon global retMSP assignment (castlings for Lion Chess)
  *retMSP = listEnd = ListMoves(stm, listStart, listEnd);
  for(i=listStart; i<listEnd; i++) {
    if(moveStack[i] == INVALID) continue;
    if(c == '@' && (moveStack[i] & SQUARE) == FROM(moveStack[i])) break; // any null move matches @@@@
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
        if(p[board[f]].value == 40) p[board[f]].promoFlag &= LAST_RANK;
        else if(!(flags & promoBoard[f]) && currentVariant != V_WOLF) moveStack[i] |= DEFER; // came from outside zone, so essential deferral
      }
    }
    if(i >= listEnd) {
      for(i=listStart; i<listEnd; i++) printf("# %d. %08x %08x %s\n", i-50, moveStack[i], ret, MoveToText(moveStack[i], 0));
      reason = NULL;
      for(i=0; i<repCnt; i++) {if((repeatMove[i] & REP_MASK) == ret) {
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
