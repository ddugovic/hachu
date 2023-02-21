/**************************************************************************/
/*                               HaChu                                    */
/* A WinBoard engine for Chu Shogi (and some related games) by H.G.Muller */
/**************************************************************************/
/* This source code is released in the public domain                      */
/**************************************************************************/

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
