/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
// TODO:
// in GenCapts we do not generate jumps of more than two squares yet

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include "board.h"
#include "eval.h"
#include "hachu.h"
#include "move.h"
#include "piece.h"
#include "types.h"
#include "variant.h"

#define HASH
#define KILLERS
#define NULLMOVE
#define CHECKEXT
#define LMR 4
#define PATH 0
#define QSDEPTH 4

#define INF 8000

#define H_UPPER 2
#define H_LOWER 1
#define FIFTY 50
#define LEVELS 200

#ifdef WIN32
#include <windows.h>
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
#include <sys/time.h>
#include <sys/ioctl.h>
     int InputWaiting()
     {
        int cnt;
        if(ioctl(0, FIONREAD, &cnt)) return 1;
        return cnt;
     }
     int GetTickCount() // with thanks to Tord
     {
        struct timeval t;
        gettimeofday(&t, NULL);
        return t.tv_sec*1000 + t.tv_usec/1000;
     }
#endif

HashBucket *hashTable;
int hashMask;

char abortFlag, fenArray[4000], startPos[4000];
int nonCapts, retFirst, retMSP, retDep, pvPtr;
int nodes, startTime, lastRootMove, lastRootIter, tlim1, tlim2, tlim3, comp;
Move ponderMove;
Move retMove, moveStack[20000], variation[FIFTY*COLORS], repStack[LEVELS+(FIFTY*COLORS)], pv[1000], repeatMove[LEVELS+(FIFTY*COLORS)], killer[FIFTY*COLORS][2];
Flag checkStack[LEVELS+(FIFTY*COLORS)];

int level, maxDepth; // used by search

// Parameters that control search behavior
int ponder;
int randomize;
int postThinking;
int noCut=1;        // engine-defined option
int resign;         // engine-defined option
int contemptFactor; // likewise
int seed;
int tsume, pvCuts, allowRep, entryProm=1, okazaki;

static inline int
PromotionFlags (Move move)
{
  MoveInfo info = MoveToInfo(move);
  int promo = promoBoard[info.to];
  if(info.path[0] != ABSENT) promo |= promoBoard[info.path[0]];
  if(info.path[1] != ABSENT) promo |= promoBoard[info.path[1]];
  return entryProm ? (promo & ~promoBoard[info.from] & CAN_PROMOTE)
                   : (promo |  promoBoard[info.from]);
}

static inline int
NewNonCapture (int from, int y, int promoFlags, int msp)
{
  Move move = MOVE(from, y);
  if(PromotionFlags(move) & promoFlags) {         // piece can promote with this move
    moveStack[msp++] = moveStack[nonCapts];       // create space for promotion
    moveStack[nonCapts++] = move | PROMOTE;       // push promotion
    if((promoFlags & promoBoard[y] & (CANT_DEFER | DONT_DEFER | LAST_RANK)) == 0) { // deferral could be a better alternative
      moveStack[msp++] = move;                    // push deferral
      if((promoBoard[from] & CAN_PROMOTE) == 0) { // enters zone
        moveStack[msp-1] |= DEFER;                // flag that promo-suppression takes place after this move
      }
    }
  } else
    moveStack[msp++] = move; // push non-promotion move
  return msp;
}

static inline int
NewCapture (int from, int y, int promoFlags, int msp)
{
  Move move = MOVE(from, y);
  if(PromotionFlags(move) & promoFlags) {         // piece can promote with this move
    moveStack[msp++] = move | PROMOTE;            // push promotion
    if((promoFlags & promoBoard[y] & (CANT_DEFER | DONT_DEFER | LAST_RANK)) == 0) { // deferral could be a better alternative
      moveStack[msp++] = move;                    // push deferral
      if((promoBoard[from] & CAN_PROMOTE) == 0) { // enters zone
        moveStack[msp-1] |= DEFER;                // flag that promo-suppression takes place after this move
      }
    }
  } else
    moveStack[msp++] = move; // push non-promotion move
  return msp;
}

char mapStep[RAYS] = { 7, 8, 1, -6, -7, -8, -1, 6 };
char rowMask[] = { 0100, 0140, 0160, 070, 034, 016, 07, 03, 01 };

int
AreaStep (int *map, int from, int x, int flags, int n, int d, int msp)
{
  int i;
  for(i=0; i<RAYS; i++) {
    int to = x + kStep[i], m = n + mapStep[i];
    if(board[to] == EDGE) continue; // off board
    if(map[m] >= d) continue;   // already done
    if(!map[m]) moveStack[msp++] = from<<SQLEN | to;
    map[m] = d;
    if(d > 1 && IsEmpty(to)) AreaStep(map, from, to, flags, m, d-1, msp);
  }
  return msp;
}

int
AreaMoves (int from, int piece, int range, int msp)
{
  int map[49]; // 7x7 map for area movers
  int i;
  for(i=0; i<49; i++) map[i] = 0;
  map[3*7+7] = range;
  return AreaStep(map, from, from, p[piece].promoFlag, 3*7+3, range, msp);
}

void
MarkBurns (Color stm, int x)
{ // make bitmap of squares in FI (7x7) neighborhood where opponents can be captured or burned
  char rows[9];
  int r=x>>5, f=x&15, top=8, bottom=0, b=0, t=8, left=0, right=8; // 9x9 area; assumes 32x16 board
  if(r < 4)      bottom =  4 - r, rows[b=bottom-1] = 0;
  else if(r > 11)   top = 19 - r, rows[t=top+1] = 0; // adjust area to board edges
  if(f < 4)        left =  4 - f;
  else if(f > 11) right = 19 - f;
  for(r=bottom; r<=top; r++) {
    int mask = 0, y = x + 16*r;
    for(f=left; f <= right; f++) {
      if(board[y + f - (4*16+4)] != EMPTY && (board[y + f - (4*16+4)] & TYPE) == INVERT(stm))
        mask |= rowMask[f]; // on-rank attacks
    }
    rows[r+2] = mask;
  }
  for(r=b; r<=t-2; r++) rows[r] |= rows[r+1] | rows[r+2]; // smear vertically
}

int
GenCastlings (Color stm, int msp)
{ // castlings for Lion Chess. Assumes board width = 8 and Kings on e-file, and K/R value = 280/300!
    int f = POS(0, BW/2), t = CASTLE;
    if(stm != WHITE) f += (bRanks*BW) - BW, t += 2;
    if(p[board[f]].value = 280) {
      if(p[board[f-4]].value == 300 && IsEmpty(f-3) && IsEmpty(f-2) && IsEmpty(f-1)) moveStack[msp++] = f<<SQLEN | t+1;
      if(p[board[f+3]].value == 300 && IsEmpty(f+1) && IsEmpty(f+2)) moveStack[msp++] = f<<SQLEN | t;
    }
    return msp;
}

int
GenNonCapts (Color stm, Move promoSuppress, int msp, Move *nullMove)
{
  int i, j;
  *nullMove = ABSENT;
  for(i=stm+2; i<=pieces[stm]; i+=2) {
    int x = p[i].pos, pFlag = p[i].promoFlag;
    if(x == ABSENT) continue;
    if(x == promoSuppress && chuFlag) pFlag = 0;
    for(j=0; j<RAYS; j++) {
      int y, v = kStep[j], r = p[i].range[j];
      if(r < 0) { // jumping piece, special treatment
        if(r == N) { // pure Knight, do off-ray jump
          if (IsEmpty(x + nStep[j])) msp = NewNonCapture(x, x + nStep[j], pFlag, msp);
        } else if(r >= S) { // in any case, do a jump of 2
          int empty = IsEmpty(x + 2*v);
          if(empty) msp = NewNonCapture(x, x + 2*v, pFlag, msp);
          if(r < I) { // Lion power, also single step
            if(IsEmpty(x + v) && (msp = NewNonCapture(x, x + v, pFlag, msp))) *nullMove = (r == W ? ABSENT : x); else empty = 0;
            if(r <= L) { // true Lion, also Knight jump
              if(empty & r < L) for(y=x+2*v; IsEmpty(y+=v) && (msp = NewNonCapture(x, y, pFlag, msp)) && r == S; ); // BS and FF moves
              v = nStep[j];
              if(r != W && IsEmpty(x + v)) msp = NewNonCapture(x, x + v, pFlag, msp);
            } else if(r >= T) { // T or K
              empty = empty && IsEmpty(x + 3*v);
              if(empty && IsEmpty(x+3*v)) msp = NewNonCapture(x, x+3*v, pFlag, msp); // Lion Dog, also triple step
              if(empty && r == K) for(y=x+3*v; IsEmpty(y+=v) && (msp = NewNonCapture(x, y, pFlag, msp)); ); // Teaching King distant moves
            }
          } else if(r == I && IsEmpty(x + v)) msp = NewNonCapture(x, x + v, pFlag, msp); // also do step
        } else if(r == M) { // FIDE Pawn; check double-move
          if(IsEmpty(x+v) && (msp = NewNonCapture(x, x+v, pFlag, msp)) && chessFlag && promoBoard[x-v] & LAST_RANK)
            if(IsEmpty(x+2*v)) msp = NewNonCapture(x, x+2*v, pFlag, msp), moveStack[msp-1] |= DEFER; // use promoSuppress flag as e.p. flag
        }
        continue;
      }
      for(y = x; r-- > 0 && IsEmpty(y+=v); )
        msp = NewNonCapture(x, y, pFlag, msp);
    }
  }
  return msp;
}

int
GenCapts (Color stm, int sqr, int victimValue, int msp)
{ // generate all moves that capture the piece on the given square
  int i, att = ATTACK(sqr, stm);
#if 0
printf("GenCapts(%c%d,%d) %08x\n", FILECH(sqr), RANK(sqr), victimValue, att);
#endif
  if(!att) return msp; // no attackers at all!
  for(i=0; i<RAYS; i++) {            // try all rays
    int x, v, jcapt=0;
    if(att & attackMask[i]) {        // attacked by move in this direction
      v = -kStep[i]; x = sqr;
      while(IsEmpty(x+=v)); // scan towards source until we encounter a 'stop'
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
          msp = NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp);
          att -= ray[i];
          if(!(att & attackMask[i])) continue; // no more; next direction
          jcapt = p[board[x]].qval;   // jump-capturer hierarchy
          while(IsEmpty(x+=v));// one attack accounted for, but more to come, so skip to next stop
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
            msp = NewCapture(x, sqr, p[attacker].promoFlag, msp);
            att -= ray[i];
          }
#if 0
if(board[x] == EDGE) { printf("    edge hit %x-%x dir=%d att=%o\n", sqr, x, i, att); continue; }
#endif
        } else if(r < 0) { // stop has non-standard moves
          switch(p[attacker].range[i]) { // figure out what he can do (including multi-captures, which causes the complexity)
            case F: // Lion power + 3-step (as in FF)
            case S: // Lion power + ranging (as in BS)
            case L: // Lion
              if(d > 2) break;
              msp = NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp);
              att -= ray[i];   // attacker is being considered
              if(d == 2) {     // victim on second ring; look for victims to take in passing
                if((board[sqr+v] & TYPE) == INVERT(stm))
                  msp = NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp);
                if(i%2 == 0) { // orthogonal: two extra bent paths
                  if((board[x+kStep[i-1]] & TYPE) == INVERT(stm))
                    msp = NewCapture(x, SPECIAL + RAY((i+RAYS-1)%RAYS, (i+1)%RAYS) + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp);
                  if((board[x+kStep[i+1]] & TYPE) == INVERT(stm))
                    msp = NewCapture(x, SPECIAL + RAY((i+1)%RAYS, (i+RAYS-1)%RAYS) + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp);
                }
              } else { // victim(s) on first ring
                int j;
                for(j=0; j<RAYS; j++) { // we can go on in 8 directions after we captured it in passing
                  int v = kStep[j];
                  if(sqr+v == x || IsEmpty(sqr+v)) { // hit & run; make sure we include igui (attacker is still at x!)
                    msp = NewCapture(x, SPECIAL + RAY(i, j) + victimValue, p[attacker].promoFlag, msp);
                  } else if((board[sqr+v] & TYPE) == INVERT(stm) && dist(x, sqr+v) == 1) { // double capture (both adjacent)
                    msp = NewCapture(x, SPECIAL + RAY(i, j) + victimValue, p[attacker].promoFlag, msp);
                  }
                }
              }
              break;
            case D: // linear Lion move (as in HF, SE)
              if(d > 2) break;
              msp = NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp);
              att -= ray[i];
              if(d == 2) { // check if we can take intermediate with it
                if((board[x-v] & TYPE) == INVERT(stm) && board[x-v] > board[sqr])
                  msp = NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p.
              } else { // d=1; can move on to second, or move back for igui
                msp = NewCapture(x, SPECIAL + RAY(i, i^4) + victimValue, p[attacker].promoFlag, msp); // igui
                if(IsEmpty(sqr-v) || (board[sqr-v] & TYPE) == INVERT(stm) && board[sqr-v] > board[sqr])
                  msp = NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // hit and run
              }
              break;
            case T: // Lion-Dog move (awful!)
            case K:
              if(d > 3) break;
              msp = NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp);
              att -= ray[i];
              if(d == 3) { // check if we can take one or two intermediates (with higher piece index) with it
                if((board[x-v] & TYPE) == INVERT(stm) && board[x-v] > board[sqr]) {
                  msp = NewCapture(x, SPECIAL + 64 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. first
                  if((board[x-2*v] & TYPE) == INVERT(stm) && board[x-2*v] > board[sqr])
                    msp = NewCapture(x, SPECIAL + 80 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. both
                } else if((board[x-2*v] & TYPE) == INVERT(stm) && board[x-2*v] > board[sqr])
                  msp = NewCapture(x, SPECIAL + 72 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. second
              } else if(d == 2) { // check if we can take intermediate with it
                if((board[x-v] & TYPE) == INVERT(stm) && board[x-v] > board[sqr]) {
                  msp = NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. first, stop at 2nd
                  msp = NewCapture(x, SPECIAL + 88 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // shoot 2nd, take 1st
                  if(IsEmpty(sqr-v) || (board[sqr-v] & TYPE) == INVERT(stm) && board[sqr-v] > board[sqr])
                    msp = NewCapture(x, SPECIAL + 80 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. 1st and 2nd
                } else if(IsEmpty(sqr-v) || (board[sqr-v] & TYPE) == INVERT(stm) && board[sqr-v] > board[sqr])
                  msp = NewCapture(x, SPECIAL + 72 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. 2nd
              } else { // d=1; can move on to second, or move back for igui
                msp = NewCapture(x, SPECIAL + RAY(i, i^4) + victimValue, p[attacker].promoFlag, msp); // igui
                if(IsEmpty(sqr-v)) { // 2nd empty
                  msp = NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. 1st and run to 2nd
                  if(IsEmpty(sqr-2*v) || (board[sqr-2*v] & TYPE) == INVERT(stm) && board[sqr-2*v] > board[sqr])
                    msp = NewCapture(x, SPECIAL + 72 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. 1st, end on 3rd
                } else if((board[sqr-v] & TYPE) == stm) { // 2nd own
                  if(IsEmpty(sqr-2*v) || (board[sqr-2*v] & TYPE) == INVERT(stm) && board[sqr-2*v] > board[sqr])
                    msp = NewCapture(x, SPECIAL + 72 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. 1st, end on 3rd
                } else if((board[sqr-v] & TYPE) == INVERT(stm) && board[sqr-v] > board[sqr]) {
                  msp = NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. 1st, capture and stop at 2nd
                  msp = NewCapture(x, SPECIAL + 88 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // shoot 2nd
                  if(IsEmpty(sqr-2*v) || (board[sqr-2*v] & TYPE) == INVERT(stm) && board[sqr-2*v] > board[sqr])
                    msp = NewCapture(x, SPECIAL + 80 + i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p. 1st and 2nd
                }
              }
              break;
            case J: // plain jump (as in KY, PH)
              if(d != 2) break;
            case I: // jump + step (as in Wa TF)
              if(d > 2) break;
              msp = NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp);
              att -= ray[i];
              break;
            case W: // jump + locust jump + 3-slide (Werewolf)
              if(d > 2) break;
              msp = NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp);
              att -= ray[i];
              if(d == 2) { // check if we can take intermediate with it
                if((board[x-v] & TYPE) == INVERT(stm) && board[x-v] > board[sqr])
                  msp = NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // e.p.
              } else { // d=1; can move on to second
                if(IsEmpty(sqr-v) || (board[sqr-v] & TYPE) == INVERT(stm) && board[sqr-v] > board[sqr])
                  msp = NewCapture(x, SPECIAL + 9*i + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp); // hit and run
              }
              break;
            case C: // FIDE Pawn
              if(d != 1) break;
              msp = NewCapture(x, sqr + victimValue - SORTKEY(attacker), p[attacker].promoFlag, msp);
              att -= ray[i];
          }
        }
//printf("mask[%d] = %o\n", i, att);
        if((att & attackMask[i]) == 0) break;
      }
      // more attacks to come; scan for next stop
      if(jcapt < p[board[x]].qval) jcapt = p[board[x]].qval; // raise barrier for range jumpers further upstream
      while(IsEmpty(x+=v)); // this should never run off-board, if the attack map is not corrupted
      }
    }
  }
  // off-ray attacks
  if(att & 0700000000) {    // Knight attack
    for(i=0; i<RAYS; i++) { // scan knight jumps to locate attacker(s)
      int from = sqr-nStep[i], attacker = board[from];
      if(attacker == EMPTY || (attacker & TYPE) != stm) continue;
      if(p[attacker].range[i] == L || p[attacker].range[i] < W && p[attacker].range[i] >= S || p[attacker].range[i] == N) { // has Knight jump in our direction
        msp = NewCapture(from, sqr + victimValue, p[attacker].promoFlag, msp); // plain jump (as in N)
        if(p[attacker].range[i] < N) { // Lion power; generate double captures over two possible intermediates
          if((board[from+kStep[i]] & TYPE) == INVERT(stm))   // left-ish path
            msp = NewCapture(from, SPECIAL + RAY(i, (i+1)%RAYS) + victimValue, p[attacker].promoFlag, msp);
          if((board[from+kStep[i+1]] & TYPE) == INVERT(stm)) // right-ish path
            msp = NewCapture(from, SPECIAL + RAY((i+1)%RAYS, i) + victimValue, p[attacker].promoFlag, msp);
        }
      }
    }
  }
  return msp;
}

static inline void
FireSet (Color c, UndoInfo *tb)
{ // set fireFlags acording to remaining presene of Fire Demons
  int i;
  for(i=c+2; DEMON(i); i++) // Fire Demons are always leading pieces in list
    if(p[i].pos != ABSENT) tb->fireMask |= fireFlags[i-2];
}

char TerminationCheck(Color stm);

Move
LookupHashMove (Color stm, int alpha, int beta, int *depth, int *lmr, Move oldPromo, Move promoSuppress, int *bestMoveNr, int *bestScore, int *iterDep, int *resDep, HashKey *index, HashKey *hit)
{
  Move hashMove;
  HashKey nr = (hashKeyL >> 30) & 3; // top 2 bits of hashKeyL
  *index = hashKeyL ^ 327*stm ^ (oldPromo + 987981)*(63121 + promoSuppress);
  *index = *index + (*index >> 16) & hashMask;
  if(hashTable[*index].lock[nr] == hashKeyH) *hit = nr;
  else if(hashTable[*index].lock[4] == hashKeyH) *hit = 4;
  else { // decide on replacement
    if(*depth >= hashTable[*index].depth[nr] ||
       *depth+1 == hashTable[*index].depth[nr] && (nodes % 4 == 0)) *hit = nr; else *hit = 4;
    return INVALID;
  }

  *bestScore = hashTable[*index].score[*hit];
  hashMove = hashTable[*index].move[*hit];

  if((*bestScore <= alpha || hashTable[*index].flag[*hit] & H_LOWER) &&
     (*bestScore >= beta  || hashTable[*index].flag[*hit] & H_UPPER)   ) {
    *iterDep = *resDep = hashTable[*index].depth[*hit];
    *bestMoveNr = 0;
    if(!level) *iterDep = 0; // no hash cutoff in root
    if(*lmr && *bestScore <= alpha && *iterDep == *depth) ++*depth, --*lmr; // self-deepening LMR
    if(pvCuts && *iterDep >= *depth && hashMove && *bestScore < beta && *bestScore > alpha)
      *iterDep = *depth - 1; // prevent hash cut in PV node
  }
  return hashMove;
}

int
Search (Color stm, int alpha, int beta, int difEval, int depth, int lmr, Move oldPromo, Move promoSuppress, int threshold, int msp)
{
  int i, j, k, king, defer, autoFail=0, late=100000, ep;
  Flag inCheck=0;
  int firstMove, curMove, sorted, bestMoveNr=0;
  int resDep=0, iterDep, ext;
  int myPV=pvPtr;
  int score, bestScore=0, oldBest, curEval, iterAlpha;
  Move move, nullMove=ABSENT;
  UndoInfo tb;
#ifdef HASH
  Move hashMove; HashKey index, hit;
#endif
/*if(PATH) pboard(board),pmap(BLACK);*/
#if 0
printf("\n# search(%d) {%d,%d} eval=%d stm=%d ",level,alpha,beta,difEval,stm);
#endif
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
      inCheck = InCheck(stm, level);
      if(!inCheck && (tsume && tsume & stm+1)) {
        retDep = 60; return INF; // we win when not in check
      }
    }
  }
#ifdef CHECKEXT
  if(inCheck && depth >= QSDEPTH) depth++;
#endif

  // KING CAPTURE
  k = p[king=royal[INVERT(stm)]].pos;
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
  curEval = Evaluate(stm, tsume, difEval) -20*inCheck;
  alpha -= (alpha < curEval);
  beta  -= (beta <= curEval);

  if(!(nodes++ & 4095)) abortFlag = TerminationCheck(stm);
  pv[pvPtr++] = 0; // start empty PV, directly behind PV of parent
  if(inCheck) lmr = 0; else depth -= lmr; // no LMR of checking moves

  firstMove = j = curMove = sorted = msp; // leave 50 empty slots in front of move list
  iterDep = -(depth == 0); tb.fireMask = 0;

#if 0
  printf("depth=%d iterDep=%d resDep=%d\n", depth, iterDep, resDep);
#endif
#ifdef HASH
  hashMove = LookupHashMove(stm, alpha, beta, &depth, &lmr, oldPromo, promoSuppress, &bestMoveNr, &bestScore, &iterDep, &resDep, &index, &hit);
#if 0
printf("# iterDep = %d score = %d hash move = %s\n",iterDep,bestScore,MoveToText(hashMove,0));
#endif
#endif

  if(depth > QSDEPTH) iterDep = MAX(iterDep, QSDEPTH); // full-width: start at least from 1-ply
  for(int phase = 0, nextVictim = INVERT(stm); ++iterDep <= depth; phase = 0, nextVictim = INVERT(stm)) {
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
if(PATH) printf("# phase=%2d: (%4d:%4d:%4d) depth=%d:%d (%d) 0x%05X\n", phase, firstMove, curMove, msp, iterDep, depth, resDep, moveStack[msp-1]);
      // MOVE SOURCE
      if(curMove >= msp) { // we ran out of moves; generate some new
        moveStack[curMove] = INVALID; // invalidate cache in case move generation fails
if(PATH) printf("# new moves (%4d:%4d:%4d) phase=%d\n", firstMove, curMove, msp, phase);
        switch(phase) {
          case 0: // null move
#ifdef NULLMOVE
            if(depth > QSDEPTH && curEval >= beta && !inCheck && filling > 10) {
              int nullDep = depth - 3;
              stm ^= WHITE;
variation[level++] = INVALID;
if(PATH) printf("%d:%d null move\n", level, depth);
              int score = -Search(stm, -beta, 1-beta, -difEval, nullDep<QSDEPTH ? QSDEPTH : nullDep, 0, promoSuppress & SQUARE, ABSENT, INF, msp);
if(PATH) printf("%d:%d null move score = %d\n", level, depth, score);
level--;
              stm ^= WHITE;
              if(score >= beta) { retDep += 3; pvPtr = myPV; return score + (score < curEval); }
//              else depth += lmr, lmr = 0;
            }
#endif
            if(tenFlag) FireSet(stm, &tb); // in tenjiku we must identify opposing Fire Demons to perform any moves
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
            nextVictim = INVERT(stm); autoFail = (depth == 0);
            phase = 3;
          case 3: // generate captures
if(PATH) printf("%d:%2d:%2d (%4d:%4d:%4d) next victim\n",level,depth,iterDep,firstMove,curMove,msp);
            while(nextVictim < pieces[INVERT(stm)]) { // more victims may exist
              int group, to = p[nextVictim += 2].pos; // take next
              if(to == ABSENT || !ATTACK(to, stm)) continue; // skip if absent or not aligned
              group = p[nextVictim].value;            // remember value of this found victim
              if(iterDep <= QSDEPTH + 1 && 2*group + curEval + 30 < alpha) {
                resDep = QSDEPTH + 1; nextVictim -= 2;
                if(bestScore < 2*group + curEval + 30) bestScore = 2*group + curEval + 30;
                goto cutoff;
              }
              msp = GenCapts(stm, to, 0, msp);
if(PATH) printf("%d:%2d:%2d (%4d:%4d:%4d) group=%d to=%c%d\n",level,depth,iterDep,firstMove,curMove,msp,group,FILECH(to),RANK(to));
              while(nextVictim < pieces[INVERT(stm)] && p[nextVictim+2].value == group) { // more victims of same value exist
                to = p[nextVictim += 2].pos;   // take next
if(PATH) printf("%d:%2d:%2d p=%d, to=%c%d\n", level, depth, iterDep, nextVictim, FILECH(to), RANK(to));
                if(to == ABSENT || !ATTACK(to, stm)) continue; // skip if absent or not aligned
                msp = GenCapts(stm, to, 0, msp);
if(PATH) printf("%d:%2d:%2d last=%d 0x%05X\n",level,depth,iterDep,msp,moveStack[msp-1]);
              }
if(PATH) printf("%d:%2d:%2d (%4d:%4d:%4d) captures %d/%d generated 0x%05X (%d)\n", level, depth, iterDep, firstMove, curMove, msp, group, threshold, moveStack[curMove], sorted);
              goto extractMove; // in auto-fail phase, only search if they might auto-fail-hi
            }
if(PATH) printf("# phase=%d autofail=%d\n", phase, autoFail);
            if(autoFail) { // non-captures cannot auto-fail; flush queued captures first
if(PATH) printf("# phase=%d autofail end (%4d:%4d:%4d)\n", phase, firstMove, curMove, msp);
              autoFail = 0; curMove = firstMove - 1; continue; // release stashed moves for search (next phase)
            }
            phase = 4; // out of victims: all captures generated
            if(chessFlag && (ep = promoSuppress & SQUARE) != ABSENT) { // e.p. rights. Create e.p. captures as Lion moves
                int n = board[ep + STEP(0, -1)], old = msp; // a-side neighbor of pushed pawn
                if( n != EMPTY && (n&TYPE) == stm && PAWN(n) ) msp = NewCapture(ep + STEP(0, -1), SPECIAL + RAY(2, stm==WHITE ? 0 : RAYS/2), 0, msp);
                n = board[ep + STEP(0, 1)];      // h-side neighbor of pushed pawn
                if( n != EMPTY && (n&TYPE) == stm && PAWN(n) ) msp = NewCapture(ep + STEP(0, 1), SPECIAL + RAY(6, stm==WHITE ? 0 : RAYS/2), 0, msp);
                if(msp != old) goto extractMove; // one or more e.p. capture were generated
            }
          case 4: // dubious captures
#if 0 // HGM
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
            msp = GenNonCapts(stm, oldPromo, msp, &nullMove);
            if(msp == nonCapts) goto cutoff;
#ifdef KILLERS
            { // swap killers to front
              Move m = killer[level][0];
              int l;
              j = curMove;
              for(l=j; l<msp; l++) if(moveStack[l] == m) { moveStack[l] = moveStack[j]; moveStack[j++] = m; break; }
              m = killer[level][1];
              for(l=j; l<msp; l++) if(moveStack[l] == m) { moveStack[l] = moveStack[j]; moveStack[j++] = m; break; }
            }
#endif
            late = j;
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
if(depth >= 0) printf("# %2d (%d) extracted 0x%05X %2d. %-10s autofail=%d\n", phase, curMove, moveStack[curMove], level, MoveToText(moveStack[curMove], 0), autoFail);
#endif

      // RECURSION
      stm ^= WHITE;
      defer = MakeMove(stm, move, &tb);
      ext = (depth == 0); // when out of depth we extend captures if there was no auto-fail-hi

//      if(level == 1 && randomize) tb.booty += (hashKeyH * seed >> 24 & 31) - 20;

      if(autoFail) {
        UnMake(&tb); // never search moves during auto-fail phase
        stm ^= WHITE;
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
          if(!level) repeatMove[repCnt++] = move & REP_MASK; // remember outlawed move
        } else { // check for perpetuals (TODO: count consecutive checks)
          Flag repCheck = inCheck;
#if 0 // HGM
          for(j=i-level; j>1; j-=2) repCheck &= checkStack[LEVELS-j];
#endif
          if(repCheck) { score = INF-20; goto repetition; } // assume perpetual check by opponent: score as win
          if(i == 2 && repStack[LEVELS+level-1] == hashKeyH) { score = INF-20; goto repetition; } // consecutive passing
        }
        score = -INF + 8*allowRep; goto repetition;
      }
      repStack[level+LEVELS] = hashKeyH;

variation[level++] = move;
mobilityScore = MapAttacks(level); // for as long as incremental update does not work.
//if(PATH) pmap(stm);
      if(chuFlag && (LION(tb.victim) || LION(tb.epVictim[0]))) {// verify legality of Lion capture in Chu Shogi
#if 0
printf("#       revalidate %d 0x%04X %s\n", level, moveStack[curMove], MoveToText(moveStack[curMove], 0));
#endif
        score = 0;
        if(LION(tb.piece)) { // Ln x Ln: can make Ln 'vulnerable' (if distant and not through intemediate > GB)
          if(dist(tb.from, tb.to) > 1 && ATTACK(tb.to, stm) && p[tb.epVictim[0]].value <= 50)
            score = -INF;               // our defended Lion is invulnerable (cannot be captured)
        } else {                        // other x Ln
          if(promoSuppress & PROMOTE) { // non-Lion captures Lion after opponent did same
            if(!okazaki || ATTACK(tb.to, stm)) score = -INF;
          }
          defer |= PROMOTE;                         // if we started, flag  he cannot do it in reply
        }
        if(score == -INF) {
          if(level == 1) repeatMove[repCnt++] = move & REP_MASK | (LION(tb.piece) ? 3<<24 : 1 << 24);
          moveStack[curMove] = INVALID; // zap illegal lion moves
          goto abortMove;
        }
      }
#if 1 // HGM
      score = -Search(stm, -beta, -iterAlpha, -difEval - tb.booty, iterDep-1+ext,
                       curMove >= late && iterDep > QSDEPTH + lmr,
                                                      promoSuppress & ~PROMOTE, defer, depth ? INF : tb.gain, msp);
#else
      score = 0;
#endif
    abortMove:
level--;
    repetition:
      UnMake(&tb);
      stm ^= WHITE;
      if(abortFlag > 0) { // unwind search
printf("# abort (%d) @ %d\n", abortFlag, level);
        if(curMove == firstMove) bestScore = oldBest, bestMoveNr = firstMove; // none searched yet
        goto leave;
      }
if(PATH) printf("%d:%2d:%2d %d 0x%05X %s %d %d (%d)\n", level, depth, iterDep, curMove, moveStack[curMove], MoveToText(moveStack[curMove], 0), score, bestScore, GetTickCount());

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
      if((abortFlag == 0 || abortFlag == 2) && GetTickCount() - startTime > tlim1) break; // do not start iteration we can (most likely) not finish
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
  pvPtr = myPV; // pop PV
  retMove = bestMoveNr ? moveStack[bestMoveNr] : INVALID;
  retDep = resDep - (inCheck & depth >= QSDEPTH) + lmr;
#if 0
printf("#       %d %s (t=%d s=%d lim=%d)\n", bestScore, MoveToText(retMove, 0), GetTickCount(), startTime, tlim1);
#endif
  return bestScore + (bestScore < curEval);
}

void
pmoves(int start, int end)
{
  int i, m, f, t;
  printf("# move stack from %d to %d\n", start, end);
  for(i=start; i<end; i++) {
    m = moveStack[i];
    f = FROM(m);
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

UndoInfo undoInfo;
int lastLift, lastPut;

Color
MakeMove2 (Color stm, Move move)
{
  int i;
  FireSet(stm, &undoInfo);
  sup0 = sup1;
  sup1 = sup2;
  sup2 = MakeMove(stm, move, &undoInfo);
  if(chuFlag && LION(undoInfo.victim) && LION(undoInfo.piece)) sup2 |= PROMOTE; // flag Lion x Lion
  rootEval = -rootEval - undoInfo.booty;
  for(i=0; i<LEVELS; i++)
    repStack[i] = repStack[i+1], checkStack[i] = checkStack[i+1];
  repStack[LEVELS-1] = hashKeyH, checkStack[LEVELS-1] = InCheck(stm, level);
#if 0
  printf("# made move %s %c%d %c%d\n", MoveToText(move, 0), FILECH(sup1), RANK(sup1), FILECH(sup2), RANK(sup2));
#endif
  return INVERT(stm);
}

Flag
InCheck (Color stm, int level)
{
  int k = p[royal[stm]].pos;
  if( k == ABSENT) k = p[royal[stm] + 2].pos;
  else if(p[royal[stm] + 2].pos != ABSENT) k = ABSENT; // two kings is no king...
  if( k != ABSENT) {
    MapAttacks(level);
    if(ATTACK(k, INVERT(stm))) return 1;
  }
  return 0;
}

void
UnMake2 (Move move)
{
  int i;
  rootEval = -rootEval - undoInfo.booty;
  UnMake(&undoInfo);
  for(i=LEVELS; i>0; i--)
    repStack[i] = repStack[i-1], checkStack[i] = checkStack[i-1];
  repStack[0] = 0, checkStack[0] = 0;
  sup2 = sup1; sup1 = sup0;
}

Color
SetUp2 (char *fen)
{
  char *p;
  Color stm = WHITE;
  if(fen) {
    char *q = strchr(fen, '\n');
    if(q) *q = '\0';
    if(q = strchr(fen, ' ')) stm = (q[1] == 'b' ? BLACK : WHITE), q[0] = '\0'; // fen contains color field
  } else fen = variant->array;
  rootEval = promoDelta = filling = cnt50 = moveNr = 0;
  SetUp(fen, variant->IDs, currentVariant);
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

int
ListMoves (Color stm, int listStart, int listEnd)
{ // create move list on move stack
#if 0
pboard(board);
#endif
  int i;
  MapAttacks(level);
  postThinking--; repCnt = 0; tlim1 = tlim2 = tlim3 = 1e8; abortFlag = listEnd = 0;
  Search(stm, -INF-1, INF+1, 0, QSDEPTH+1, 0, sup1 & ~PROMOTE, sup2, INF, listEnd);
  postThinking++;

#if 0
printf("last=%d nc=%d retMSP=%d\n", listEnd, nonCapts, retMSP);
#endif
  listEnd = retMSP;
  if(currentVariant == V_LION) listEnd = GenCastlings(stm, listEnd); // castlings for Lion Chess
  if(currentVariant == V_WOLF) for(i=listStart; i<listEnd; i++) { // mark Werewolf captures as promotions
    int to = moveStack[i] & SQUARE, from = FROM(moveStack[i]);
    if(to >= SPECIAL) continue;
    if(p[board[to]].ranking >= 5 && p[board[from]].ranking < 4) moveStack[i] |= PROMOTE;
  }
  return listEnd;
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
    if(sqr == FROM(moveStack[i])) {
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
      if(lastLift == FROM(moveStack[i])) {
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
SearchBestMove (Color stm, Move *move, Move *ponderMove, int retMSP)
{
  int score;
printf("# SearchBestMove\n");
  startTime = GetTickCount();
  nodes = 0;
//printf("# s=%d\n", startTime);fflush(stdout);
  MapAttacks(level);
  retMove = INVALID; repCnt = 0;
  score = Search(stm, -INF-1, INF+1, rootEval, maxDepth + QSDEPTH, 0, sup1, sup2, INF, retMSP);
  *move = retMove;
  *ponderMove = pv[1];
printf("# best=%s", MoveToText(pv[0],0));
if(pv[1]) printf(" ponder=%s", MoveToText(pv[1],0));
printf("\n");
  return score;
}

    Color TakeBack (int n)
    { // reset the game and then replay it to the desired point
      int last;
      Color stm;
      last = moveNr - n; if(last < 0) last = 0;
      Init(SAME); stm = SetUp2(startPos);
printf("# setup done");fflush(stdout);
      for(moveNr=0; moveNr<last; moveNr++) stm = MakeMove2(stm, gameMove[moveNr]),printf("make %2d: %x\n", moveNr, gameMove[moveNr]);
      return stm;
    }

    void PrintResult(Color stm, int score)
    {
      char tail[100];
      if(reason) sprintf(tail, " {%s}", reason); else *tail = 0;
      if(score == 0) printf("1/2-1/2%s\n", tail);
      if(score > 0 && stm == WHITE || score < 0 && stm == BLACK) printf("1-0%s\n", tail);
      else printf("0-1%s\n", tail);
    }

    void GetLine(Color stm, Flag root)
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
#if 0
printf("# in (mode = %d,%d): %s\n", root, abortFlag, command);
#endif
        if(!strcmp(command, "otim"))    { continue; } // do not start pondering after receiving time commands, as move will follow immediately
        if(!strcmp(command, "time"))    { sscanf(inBuf, "time %d", &timeLeft); continue; }
        if(!strcmp(command, "put"))     { ReadSquare(inBuf+4, &lastPut); continue; }  // ditto
        if(!strcmp(command, "."))       { inBuf[0] = 0; return; } // ignore for now
        if(!strcmp(command, "hover"))   { inBuf[0] = 0; return; } // ignore for now
        if(!strcmp(command, "lift"))    { inBuf[0] = 0; retMSP = ListMoves(stm, retFirst, retMSP); Highlight(retFirst, retMSP, inBuf+5); return; } // treat here
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

    char
    TerminationCheck (Color stm)
    {
      if(abortFlag < 0) { // pondering; check for input
        if(InputWaiting()) GetLine(stm, 0); // read & examine input command
      } else {            // check for time
        if(GetTickCount() - startTime > tlim3) abortFlag = 2;
      }
      return abortFlag;
    }

    int
    main ()
    {
      int engineSide=NONE;                // side played by engine
      Color stm=BLACK;
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
        if(retMSP == 0) retMSP = ListMoves(stm, retFirst, retMSP); // always maintain a list of legal moves in root position
        abortFlag = -(ponder && INVERT(stm) == engineSide && moveNr); // pondering and opponent on move
        if(stm == engineSide || abortFlag && ponderMove) {         // if it is the engine's turn to move, set it thinking, and let it move
printf("# start %s: stm=%d engine=%d ponder=%d\n", abortFlag == -1 ? "ponder" : "search", stm, engineSide, ponder);
          if(abortFlag) {
            stm = MakeMove2(stm, ponderMove);                           // for pondering, play speculative move
            gameMove[moveNr++] = ponderMove;                            // remember in game
            sprintf(ponderMoveText, "%s\n", MoveToText(ponderMove, 0)); // for detecting ponder hits
printf("# ponder=%s\n", ponderMoveText);
          } else SetSearchTimes(10*timeLeft);                           // for thinking, schedule end time
#if 0
pboard(board);
#endif
          score = SearchBestMove(stm, &move, &ponderMove, retMSP);
          if(abortFlag == 1) { // ponder search was interrupted (and no hit)
            UnMake2(INVALID); moveNr--; stm ^= WHITE;    // take ponder move back if we made one
            abortFlag = 0;
          } else if(move == INVALID) {                   // game apparently ended
            int kcapt = 0, king, k = p[king=royal[INVERT(stm)]].pos;
            if( k != ABSENT) { // test if King capture possible
              if(ATTACK(k, stm)) {
                if( p[king + 2].pos == ABSENT ) kcapt = 1; // we have an attack on his only King
              }
            } else { // he has no king! Test for attacks on Crown Prince
              k = p[king + 2].pos;
              if(ATTACK(k, stm)) kcapt = 1; // we have attack on Crown Prince
            }
            if(kcapt) { // print King capture before claiming
              int msp = GenCapts(stm, k, 0, retMSP);
              printf("move %s\n", MoveToText(moveStack[msp-1], 1));
              reason = "king capture";
            } else reason = "resign";
            engineSide = NONE;          // so stop playing
            PrintResult(stm, score);
          } else if(abortFlag >= 0) {   // prevents accidental self-play
            int f;
            Move pMove = move;
            static char *pName[] = { "w", "z", "j" };
            if((move & SQUARE) >= SPECIAL && PAWN(board[f = FROM(move)])) { // e.p. capture
              pMove = move & ~SQUARE | f + toList[(move & SQUARE) - SPECIAL]; // print as a single move
            }
            stm = MakeMove2(stm, move);  // assumes MakeMove returns new side to move
            gameMove[moveNr++] = move;   // remember game
            i = p[undoInfo.victim].ranking;
            printf("move %s%s\n", MoveToText(pMove, 1), i == 5 && p[undoInfo.piece].ranking < 4 ? pName[i-5] : "");
            retMSP = 0;                  // list has been printed
            continue;                    // go check if we should ponder
          }
        } else if(engineSide == ANALYZE || abortFlag) { // in analysis, we always ponder the position
            Move dummy;
            *ponderMoveText = 0; // forces miss on any move
            abortFlag = -1;      // set pondering
            pvCuts = noCut;
            SearchBestMove(stm, &dummy, &dummy, retMSP);
            abortFlag = pvCuts = 0;
        }

        if(fflush(stdout) == EOF) break; // make sure everything is printed before we do something that might take time
        if(!*inBuf) GetLine(stm, 1); // takes care of time and otim commands

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
        if(!strcmp(command, "w"))       { MapAttacksByColor(WHITE, pieces[WHITE], level); pmap(WHITE); continue; }
        if(!strcmp(command, "b"))       { MapAttacksByColor(BLACK, pieces[BLACK], level); pmap(BLACK); continue; }
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
          int move = ParseMove(stm, retFirst, retMSP, inBuf+9, moveStack, repeatMove, &retMSP);
#if 0
pboard(board);
#endif
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
          engineSide = BLACK; Init(V_CHESS); stm = SetUp2(NULL); maxDepth = MAXPLY; randomize = OFF; curVarNr = comp = 0;
          continue;
        }
        if(!strcmp(command, "variant")) {
          for(i=0; variants[i].boardRanks; i++) {
            sscanf(inBuf+8, "%s", command);
            if(!strcmp(variants[i].name, command)) {
              Init(curVarNr = i); stm = SetUp2(NULL); break;
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
        if(!strcmp(command, "setboard")){ engineSide = NONE; Init(curVarNr); stm = SetUp2(inBuf+9); continue; }
        if(!strcmp(command, "undo"))    { stm = TakeBack(1); continue; }
        if(!strcmp(command, "remove"))  { stm = TakeBack(2); continue; }
        printf("Error: unknown command\n");
      }
      return 0;
    }

