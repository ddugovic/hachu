/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/
#ifndef VARIANT_H
#define VARIANT_H
#include "types.h"

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

extern PieceDesc chuPieces[];
extern PieceDesc shoPieces[];
extern PieceDesc daiPieces[];
extern PieceDesc waPieces[];
extern PieceDesc ddPieces[];
extern PieceDesc makaPieces[];
extern PieceDesc taiPieces[];
extern PieceDesc tenjikuPieces[];
extern PieceDesc taikyokuPieces[];
extern PieceDesc chessPieces[];
extern PieceDesc lionPieces[];
extern PieceDesc shatranjPieces[];
extern PieceDesc makrukPieces[];
extern PieceDesc wolfPieces[];

extern char chuArray[];
extern char daiArray[];
extern char tenArray[];
extern char cashewArray[];
extern char macadArray[];
extern char shoArray[];
extern char waArray[];
extern char chessArray[];
extern char lionArray[];
extern char shatArray[];
extern char thaiArray[];
extern char wolfArray[];

// translation tables for single-(dressed-)letter IDs to multi-letter names, per variant
//                 A.B.C.D.E.F.G.H.I.J.K.L.M.N.O.P.Q.R.S.T.U.V.W.X.Y.Z.
extern char chuIDs[];
extern char daiIDs[];
extern char tenIDs[];
extern char waIDs[];
extern char chessIDs[];
extern char makaIDs[];
extern char dadaIDs[];

typedef enum { V_CHESS, V_SHO, V_CHU, V_DAI, V_DADA, V_MAKA, V_TAI, V_KYOKU, V_TENJIKU,
	       V_CASHEW, V_MACAD, V_SHATRANJ, V_MAKRUK, V_LION, V_WA, V_WOLF } Variant;
#define SAME (-1)

extern VariantDesc variants[];
#endif
