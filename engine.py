# absorption_minimax.py
from typing import List, Tuple, Optional
import math
import copy
from server import *
# --- reuse your enums/dataclasses ---
# from your_module import PieceType, Color, Piece, ChessGame
# (Assume they are already defined in the runtime.)

# Centipawn piece base values (Stockfish-like)
PIECE_VALUES = {
    PieceType.PAWN: 100,
    PieceType.KNIGHT: 320,
    PieceType.BISHOP: 330,
    PieceType.ROOK: 500,
    PieceType.QUEEN: 900,
    PieceType.KING: 20000,
}

# ------------------------------
# Utility: generate all legal moves using your _is_valid_move
# ------------------------------
def generate_all_moves(game: "ChessGame", color: Color) -> List[Tuple[Tuple[int,int], Tuple[int,int]]]:
    moves = []
    for r in range(8):
        for c in range(8):
            p = game.board[r][c]
            if p and p.color == color:
                for tr in range(8):
                    for tc in range(8):
                        if (r, c) == (tr, tc):
                            continue
                        # use user's legality checker
                        try:
                            if game._is_valid_move(p, (r, c), (tr, tc)):
                                moves.append(((r,c), (tr,tc)))
                        except Exception:
                            # If _is_valid_move depends on game state not fully set,
                            # skip exceptions rather than crash the search.
                            pass
    return moves

# ------------------------------
# Apply and undo moves (record minimal state to restore)
# ------------------------------
class MoveState:
    def __init__(self, move, moved_piece_prev_has_moved, moved_piece_prev_abilities, captured_piece):
        self.move = move
        self.moved_piece_prev_has_moved = moved_piece_prev_has_moved
        self.moved_piece_prev_abilities = moved_piece_prev_abilities
        self.captured_piece = captured_piece

def apply_move(game: "ChessGame", move: Tuple[Tuple[int,int], Tuple[int,int]]) -> MoveState:
    (r1,c1),(r2,c2) = move
    mover: Piece = game.board[r1][c1]
    captured: Optional[Piece] = game.board[r2][c2]

    # Save previous state
    prev_has_moved = mover.has_moved
    prev_abilities = mover.abilities.copy()

    # Perform capture + absorption
    if captured:
        # deep-copy captured to restore safely
        captured_copy = copy.deepcopy(captured)
        for ab in captured.abilities:
            if ab not in mover.abilities:
                mover.abilities.append(ab)
    else:
        captured_copy = None

    # Move piece
    mover.position = (r2,c2)
    mover.has_moved = True
    game.board[r2][c2] = mover
    game.board[r1][c1] = None

    # flip turn
    game.current_turn = Color.WHITE if game.current_turn == Color.BLACK else Color.BLACK

    return MoveState(move, prev_has_moved, prev_abilities, captured_copy)

def undo_move(game: "ChessGame", state: MoveState):
    (r1,c1),(r2,c2) = state.move
    mover: Piece = game.board[r2][c2]

    # restore mover
    mover.position = (r1,c1)
    mover.has_moved = state.moved_piece_prev_has_moved
    mover.abilities = state.moved_piece_prev_abilities.copy()

    game.board[r1][c1] = mover
    # restore captured piece (or None)
    game.board[r2][c2] = state.captured_piece

    # flip turn back
    game.current_turn = Color.WHITE if game.current_turn == Color.BLACK else Color.BLACK

# ------------------------------
# Terminal detection
# ------------------------------
def is_terminal(game: "ChessGame") -> Optional[int]:
    """
    Return None if not terminal.
    If terminal, return an evaluation (centipawns) from White's POV:
      large positive => White wins; large negative => Black wins.
    We'll treat missing king as immediate decisive result.
    """
    white_king = False
    black_king = False
    for r in range(8):
        for c in range(8):
            p = game.board[r][c]
            if p and p.type == PieceType.KING:
                if p.color == Color.WHITE:
                    white_king = True
                else:
                    black_king = True
    if not white_king:
        return -PIECE_VALUES[PieceType.KING]  # White lost
    if not black_king:
        return PIECE_VALUES[PieceType.KING]   # Black lost
    return None

# ------------------------------
# Evaluation function (DeepBlue-style, absorption-aware)
# ------------------------------
def evaluate(game: "ChessGame") -> int:
    """
    Returns centipawn evaluation from White's POV.
    Components:
     - Material (base + absorbed abilities)
     - Synergy bonus (multi-ability piece)
     - Mobility (small weight)
     - Pawn structure: doubled/isolated/passed pawns
     - King safety: pawn shield count in front of king
    """
    score = 0

    # Material & synergy
    for r in range(8):
        for c in range(8):
            p = game.board[r][c]
            if not p:
                continue
            # Unique abilities set (avoid duplicates)
            uniq = set(p.abilities)
            # Sum of abilities' base values (so a piece's "worth" is sum of the abilities it has)
            val = 0
            for ab in uniq:
                val += PIECE_VALUES.get(ab, 0)
            # If abilities don't include the piece's own type (edge case), ensure base included
            if p.type not in uniq:
                val += PIECE_VALUES[p.type]

            # synergy: reward combining diverse abilities (diminishing returns)
            ability_count = len(uniq)
            if ability_count >= 2:
                # e.g. rook + bishop -> queen-like synergy
                synergy = int(100 * (ability_count - 1))  # 100 cp per extra distinct ability
                val += synergy

            score += val if p.color == Color.WHITE else -val

    # Mobility
    white_moves = len(generate_all_moves(game, Color.WHITE))
    black_moves = len(generate_all_moves(game, Color.BLACK))
    mobility_weight = 6  # centipawns per legal move difference
    score += (white_moves - black_moves) * mobility_weight

    # Pawn structure (simple)
    # For each color compute doubled, isolated, passed pawn penalties/bonuses
    def pawn_structure_score(color: Color) -> int:
        files = {f: [] for f in range(8)}  # file -> list of pawn rows
        for r in range(8):
            for c in range(8):
                p = game.board[r][c]
                if p and p.type == PieceType.PAWN and p.color == color:
                    files[c].append(r)
        s = 0
        for f in range(8):
            pawns_in_file = files[f]
            if len(pawns_in_file) > 1:
                s -= 25 * (len(pawns_in_file) - 1)  # doubled pawn penalty per extra pawn

            # isolated: no friendly pawn in adjacent files
            if pawns_in_file:
                if not files.get(f - 1) and not files.get(f + 1):
                    s -= 15 * len(pawns_in_file)

            # passed pawns (very simple): no opposing pawn on same file ahead of pawn
            for pr in pawns_in_file:
                blocked = False
                for opp_r in range(8):
                    opp = None
                    # find opponent pawn in same file
                    opp = game.board[opp_r][f]
                    if opp and opp.type == PieceType.PAWN and opp.color != color:
                        # determine "ahead" depending on color orientation
                        if color == Color.WHITE:
                            if opp_r < pr:  # opponent pawn is in front of this white pawn
                                blocked = True
                                break
                        else:
                            if opp_r > pr:
                                blocked = True
                                break
                if not blocked:
                    s += 40  # passed pawn bonus
        return s

    score += pawn_structure_score(Color.WHITE)
    score -= pawn_structure_score(Color.BLACK)  # subtract opponent pawn structure score

    # King safety (very simple pawn-shield based)
    def king_safety_score(color: Color) -> int:
        # find king
        kr, kc = None, None
        for r in range(8):
            for c in range(8):
                p = game.board[r][c]
                if p and p.type == PieceType.KING and p.color == color:
                    kr, kc = r, c
                    break
            if kr is not None:
                break
        if kr is None:
            return 0
        shield_count = 0
        # For white, pawns are expected to be on rows < king row (moving up); for black opposite.
        if color == Color.WHITE:
            candidates = [(kr-1, kc-1),(kr-1, kc),(kr-1, kc+1)]
        else:
            candidates = [(kr+1, kc-1),(kr+1, kc),(kr+1, kc+1)]
        for (sr, sc) in candidates:
            if 0 <= sr < 8 and 0 <= sc < 8:
                sp = game.board[sr][sc]
                if sp and sp.type == PieceType.PAWN and sp.color == color:
                    shield_count += 1
        # each shield pawn reduces vulnerability by some cp
        return shield_count * 30

    score += king_safety_score(Color.WHITE)
    score -= king_safety_score(Color.BLACK)

    return int(score)

# ------------------------------
# Minimax with alpha-beta and simple move ordering (captures first)
# ------------------------------
def minimax(game: "ChessGame", depth: int, alpha: int, beta: int, maximizing_player: bool) -> Tuple[int, Optional[Tuple[Tuple[int,int],Tuple[int,int]]]]:
    # Terminal test
    term = is_terminal(game)
    if term is not None:
        return term, None

    if depth == 0:
        return evaluate(game), None

    color = Color.WHITE if maximizing_player else Color.BLACK
    moves = generate_all_moves(game, color)

    if not moves:
        # no moves -> return static eval (stalemate or checkmate handled by is_terminal)
        return evaluate(game), None

    # Basic move ordering: prefer captures (makes alpha-beta faster)
    def move_score_maybe_capture(m):
        (r1,c1),(r2,c2) = m
        tgt = game.board[r2][c2]
        if tgt:
            return PIECE_VALUES.get(tgt.type, 0)
        return 0
    moves.sort(key=move_score_maybe_capture, reverse=True)

    best_move = None

    if maximizing_player:
        max_eval = -math.inf
        for mv in moves:
            state = apply_move(game, mv)
            val, _ = minimax(game, depth - 1, alpha, beta, False)
            undo_move(game, state)
            if val > max_eval:
                max_eval = val
                best_move = mv
            alpha = max(alpha, val)
            if alpha >= beta:
                break
        return int(max_eval), best_move
    else:
        min_eval = math.inf
        for mv in moves:
            state = apply_move(game, mv)
            val, _ = minimax(game, depth - 1, alpha, beta, True)
            undo_move(game, state)
            if val < min_eval:
                min_eval = val
                best_move = mv
            beta = min(beta, val)
            if alpha >= beta:
                break
        return int(min_eval), best_move

# ------------------------------
# Public wrapper: find_best_move(game, depth)
# ------------------------------
def find_best_move(game: "ChessGame", depth: int) -> Tuple[Optional[Tuple[Tuple[int,int],Tuple[int,int]]], int]:
    """
    Returns: (best_move, eval_score_in_centipawns)
    best_move is ((r1,c1),(r2,c2)) or None.
    """
    maximizing = (game.current_turn == Color.WHITE)
    eval_score, best_move = minimax(game, depth, -10**9, 10**9, maximizing)
    return best_move, int(eval_score)

# ------------------------------
# Example usage (if run standalone)
# ------------------------------
if __name__ == "__main__":
    g = ChessGame()  # your class
    mv, score = find_best_move(g, depth=3)
    print("Best move:", mv, "Eval:", score)
