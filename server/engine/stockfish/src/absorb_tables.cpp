/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Absorb Chess modification for precomputed lookup tables
*/

#include "types.h"

namespace AbsorbChess {

// Lookup tables
PieceType MobilityTypes[PIECE_TYPE_NB][MAX_ABILITY_COMBINATIONS][4];
int MobilityTypeCount[PIECE_TYPE_NB][MAX_ABILITY_COMBINATIONS];
PieceType EffectiveMaterialType[PIECE_TYPE_NB][MAX_ABILITY_COMBINATIONS];
PieceType EffectivePSQTType[PIECE_TYPE_NB][MAX_ABILITY_COMBINATIONS];

void init_tables() {
  // Initialize all arrays to default values
  for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
    for (int abilities = 0; abilities < MAX_ABILITY_COMBINATIONS; ++abilities) {
      MobilityTypeCount[pt][abilities] = 0;
      for (int i = 0; i < 4; ++i) {
        MobilityTypes[pt][abilities][i] = NO_PIECE_TYPE;
      }
      EffectiveMaterialType[pt][abilities] = PieceType(pt);
      EffectivePSQTType[pt][abilities] = PieceType(pt);
    }
  }
  
  // Build lookup tables for each piece type and ability combination
  for (int pt = PAWN; pt <= KING; ++pt) {
    for (int abilities = 0; abilities < MAX_ABILITY_COMBINATIONS; ++abilities) {
      PieceType basePt = PieceType(pt);
      
      // Calculate effective types based on absorb chess rules
      PieceType materialType = basePt;
      PieceType psqtType = basePt;
      
      // Mobility calculation: determine which movement patterns to include
      int mobCount = 0;
      
      switch (basePt) {
        case PAWN:
          // Pawn always moves like a pawn
          MobilityTypes[pt][abilities][mobCount++] = PAWN;
          
          // Pawn with any major piece ability becomes that piece for material/psqt
          if (abilities & ABILITY_QUEEN) {
            materialType = psqtType = QUEEN;
            MobilityTypes[pt][abilities][mobCount++] = QUEEN;
          } else if (abilities & ABILITY_ROOK) {
            materialType = psqtType = ROOK;
            MobilityTypes[pt][abilities][mobCount++] = ROOK;
          } else if (abilities & ABILITY_BISHOP) {
            materialType = psqtType = BISHOP;
            MobilityTypes[pt][abilities][mobCount++] = BISHOP;
          } else if (abilities & ABILITY_KNIGHT) {
            materialType = psqtType = KNIGHT;
            MobilityTypes[pt][abilities][mobCount++] = KNIGHT;
          }
          break;
          
        case KNIGHT:
          // Knight always has knight movement
          MobilityTypes[pt][abilities][mobCount++] = KNIGHT;
          
          // Knight + Bishop = Queen
          if (abilities & ABILITY_BISHOP) {
            materialType = psqtType = QUEEN;
            MobilityTypes[pt][abilities][mobCount++] = BISHOP;
            // Don't add rook separately if we have both bishop and rook
            if (abilities & ABILITY_ROOK) {
              MobilityTypes[pt][abilities][mobCount++] = ROOK;
            }
          }
          // Knight + Rook (without bishop) = Enhanced Rook material, but keep knight+rook mobility
          else if (abilities & ABILITY_ROOK) {
            materialType = ROOK; // Material calculation as rook
            psqtType = ROOK;     // Position evaluation as rook
            MobilityTypes[pt][abilities][mobCount++] = ROOK;
          }
          break;
          
        case BISHOP:
          // Bishop always has bishop movement
          MobilityTypes[pt][abilities][mobCount++] = BISHOP;
          
          // Bishop + Rook = Queen
          if (abilities & ABILITY_ROOK) {
            materialType = psqtType = QUEEN;
            MobilityTypes[pt][abilities][mobCount++] = ROOK;
          }
          // Bishop + Knight = Queen
          else if (abilities & ABILITY_KNIGHT) {
            materialType = psqtType = QUEEN;
            MobilityTypes[pt][abilities][mobCount++] = KNIGHT;
          }
          break;
          
        case ROOK:
          // Rook always has rook movement
          MobilityTypes[pt][abilities][mobCount++] = ROOK;
          
          // Rook + Bishop = Queen
          if (abilities & ABILITY_BISHOP) {
            materialType = psqtType = QUEEN;
            MobilityTypes[pt][abilities][mobCount++] = BISHOP;
          }
          // Rook + Knight (without bishop) = Enhanced Rook with knight mobility
          else if (abilities & ABILITY_KNIGHT) {
            // Stay as rook for material/psqt, but add knight mobility
            MobilityTypes[pt][abilities][mobCount++] = KNIGHT;
          }
          break;
          
        case QUEEN:
          // Queen always has queen movement (rook + bishop)
          MobilityTypes[pt][abilities][mobCount++] = QUEEN;
          
          // Queen with knight adds knight mobility
          if (abilities & ABILITY_KNIGHT) {
            MobilityTypes[pt][abilities][mobCount++] = KNIGHT;
          }
          // Queen already has rook and bishop, so no additional material value
          materialType = psqtType = QUEEN;
          break;
          
        case KING:
          // King always moves like king, no changes
          MobilityTypes[pt][abilities][mobCount++] = KING;
          materialType = psqtType = KING;
          break;
          
        default:
          break;
      }
      
      // Store the computed values
      MobilityTypeCount[pt][abilities] = mobCount;
      EffectiveMaterialType[pt][abilities] = materialType;
      EffectivePSQTType[pt][abilities] = psqtType;
    }
  }
}

} // namespace AbsorbChess