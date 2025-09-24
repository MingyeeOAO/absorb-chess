# absorption_chess_engine.py
"""
Absorption Chess Engine - A minimax-based AI engine for absorption chess

This engine uses minimax with alpha-beta pruning to find the best moves in absorption chess,
where pieces can absorb the abilities of captured pieces, creating dynamic piece values.
"""

from re import I
from typing import List, Tuple, Optional, Dict, NamedTuple
import math
import copy
from server.core.game import ChessGame
from server.core.enums import PieceType, Color
from server.core.piece import Piece
import time
import random

# Centipawn piece base values (Stockfish-like)
# Centipawn piece base values (Stockfish-like)
PIECE_VALUES = {
    PieceType.PAWN: 100,
    PieceType.KNIGHT: 320,
    PieceType.BISHOP: 3300,
    PieceType.ROOK: 500,
    PieceType.QUEEN: 900,
    PieceType.KING: 20000,
}
INF = 1000000



# 只顯示整個 Engine 類（可整段替換原本 Engine 類）

# TT flags
EXACT = 0
LOWERBOUND = 1
UPPERBOUND = 2

class TTEntry(NamedTuple):
    depth: int
    flag: int
    value: int
    best_move: Optional[Tuple[Tuple[int,int],Tuple[int,int]]]

class MoveState:
    def __init__(self, move,
                 moved_piece_prev_has_moved,
                 moved_piece_prev_abilities,
                 captured_piece_ref,
                 captured_prev_position,
                 captured_prev_has_moved,
                 captured_prev_abilities):
        self.move = move
        self.moved_piece_prev_has_moved = moved_piece_prev_has_moved
        # shallow copy of abilities list (list of primitives/strings)
        self.moved_piece_prev_abilities = list(moved_piece_prev_abilities)
        # if captured_piece_ref is None, it's a quiet move
        self.captured_piece_ref = captured_piece_ref
        self.captured_prev_position = captured_prev_position
        self.captured_prev_has_moved = captured_prev_has_moved
        self.captured_prev_abilities = list(captured_prev_abilities) if captured_prev_abilities is not None else None


class TranspositionTable:
    def __init__(self):
        self.table = {}

    def lookup(self, key: int):
        return self.table.get(key)

    def store(self, key: int, value: int, depth: int, flag: str, best_move=None):
        self.table[key] = TTEntry(value, depth, flag, best_move)

class Engine():
    def __init__(self, game: ChessGame):
        self.game = game
        self.transposition_table = TranspositionTable()
        self.root_depth = 0
        self.node_count = 0
        # Transposition table
        self.tt: Dict[int, TTEntry] = {}
        #self.best_move : Optional[Tuple[Tuple[int,int],Tuple[int,int]]] = None
        # Zobrist initialization
        random.seed(0xC0FFEE)  # 固定種子，方便除錯（可改為 None）
        # zobrist_piece[sq_index][piece_type_value][color_value] -> 64bit
        self.zobrist_piece = {}
        for sq in range(64):
            self.zobrist_piece[sq] = {}
            for pt in range(1, 7):  # assume PieceType enum values 1..6
                self.zobrist_piece[sq][pt] = {}
                self.zobrist_piece[sq][pt][0] = random.getrandbits(64)
                self.zobrist_piece[sq][pt][1] = random.getrandbits(64)
        # ability random map (dynamic)
        self.zobrist_ability: Dict[str, int] = {}
        # side to move random
        self.zobrist_side = random.getrandbits(64)

        # current zobrist key (init from board)
        self.zobrist_key = self.compute_full_zobrist()

        # killer moves: killer[depth] -> list of up to 2 killer moves
        self.killer: Dict[int, List[Tuple[Tuple[int,int],Tuple[int,int]]]] = {}
        # history heuristic: history[(from_sq,to_sq)] -> score
        self.history: Dict[Tuple[int,int], int] = {}

    # ---------- Zobrist helpers ----------
    def ability_rand(self, ability: str) -> int:
        if ability not in self.zobrist_ability:
            self.zobrist_ability[ability] = random.getrandbits(64)
        return self.zobrist_ability[ability]

    def sq_index(self, r: int, c: int) -> int:
        return r * 8 + c

    def piece_type_index(self, p: Piece) -> int:
        # Map PieceType enum to integer 1..6
        piece_type_map = {
            PieceType.PAWN: 1,
            PieceType.ROOK: 2,
            PieceType.KNIGHT: 3,
            PieceType.BISHOP: 4,
            PieceType.QUEEN: 5,
            PieceType.KING: 6
        }
        return piece_type_map.get(p.type, 1)  # Default to 1 if not found

    def color_index(self, p: Piece) -> int:
        return 0 if p.color == Color.WHITE else 1

    def compute_full_zobrist(self) -> int:
        key = 0
        for r in range(8):
            for c in range(8):
                p = self.game.board[r][c]
                if not p:
                    continue
                sq = self.sq_index(r, c)
                pt_idx = self.piece_type_index(p)
                col_idx = self.color_index(p)
                key ^= self.zobrist_piece[sq][pt_idx][col_idx]
                # abilities
                for ab in set(p.abilities):
                    key ^= self.ability_rand(str(ab))
        # side to move
        if self.game.current_turn == Color.BLACK:
            key ^= self.zobrist_side
        return key

    # incremental updates inside apply_move/undo_move: use xor to remove/add
    def xor_piece_at(self, key: int, r: int, c: int, p: Optional[Piece]) -> int:
        if p is None:
            return key
        sq = self.sq_index(r, c)
        pt_idx = self.piece_type_index(p)
        col_idx = self.color_index(p)
        key ^= self.zobrist_piece[sq][pt_idx][col_idx]
        for ab in set(p.abilities):
            key ^= self.ability_rand(str(ab))
        return key

    # ------------------------------
    # Apply and undo moves (record minimal state to restore)  -- modified to update zobrist
    # ------------------------------
    
    # apply_move: 不做 deepcopy，只記錄被吃掉物件的必要欄位來還原
    def apply_move(self, move: Tuple[Tuple[int,int], Tuple[int,int]]) -> MoveState:
        (r1,c1),(r2,c2) = move
        mover: Piece = self.game.board[r1][c1]
        captured: Optional[Piece] = self.game.board[r2][c2]

        # Save previous state for mover
        prev_has_moved = mover.has_moved
        prev_abilities = mover.abilities.copy()  # shallow copy of abilities list

        # Zobrist: xor out mover from source square
        self.zobrist_key = self.xor_piece_at(self.zobrist_key, r1, c1, mover)

        if captured:
            # Save captured minimal state (no deepcopy)
            captured_ref = captured
            captured_prev_pos = captured.position
            captured_prev_has_moved = captured.has_moved
            captured_prev_abilities = captured.abilities.copy()

            # xor out captured from destination square
            self.zobrist_key = self.xor_piece_at(self.zobrist_key, r2, c2, captured_ref)

            # Absorb abilities (append only new ones)
            for ab in captured_ref.abilities:
                if ab not in mover.abilities:
                    mover.abilities.append(ab)
        else:
            captured_ref = None
            captured_prev_pos = None
            captured_prev_has_moved = None
            captured_prev_abilities = None

        # Move piece on board (update mover fields)
        mover.position = (r2, c2)
        mover.has_moved = True
        self.game.board[r2][c2] = mover
        self.game.board[r1][c1] = None

        # Zobrist: xor in mover at destination (with possibly new abilities)
        self.zobrist_key = self.xor_piece_at(self.zobrist_key, r2, c2, mover)

        # flip side in zobrist and game
        self.zobrist_key ^= self.zobrist_side
        self.game.current_turn = Color.WHITE if self.game.current_turn == Color.BLACK else Color.BLACK

        return MoveState(move, prev_has_moved, prev_abilities,
                        captured_ref, captured_prev_pos, captured_prev_has_moved, captured_prev_abilities)

    # undo_move: 用保存的最小欄位回復狀態
    def undo_move(self, state: MoveState):
        (r1,c1),(r2,c2) = state.move
        mover: Piece = self.game.board[r2][c2]

        # xor out mover at dest (current state)
        self.zobrist_key = self.xor_piece_at(self.zobrist_key, r2, c2, mover)

        # restore mover fields
        mover.position = (r1, c1)
        mover.has_moved = state.moved_piece_prev_has_moved
        mover.abilities = list(state.moved_piece_prev_abilities)

        # put mover back to source square
        self.game.board[r1][c1] = mover

        # restore captured (if any)
        if state.captured_piece_ref:
            cap = state.captured_piece_ref
            # restore captured attributes
            cap.position = state.captured_prev_position
            cap.has_moved = state.captured_prev_has_moved
            cap.abilities = list(state.captured_prev_abilities)
            # place captured back to destination square
            self.game.board[r2][c2] = cap

            # xor in the captured at dest
            self.zobrist_key = self.xor_piece_at(self.zobrist_key, r2, c2, cap)
        else:
            # destination is empty now
            self.game.board[r2][c2] = None

        # xor in mover at source square
        self.zobrist_key = self.xor_piece_at(self.zobrist_key, r1, c1, mover)

        # flip side back
        self.zobrist_key ^= self.zobrist_side
        self.game.current_turn = Color.WHITE if self.game.current_turn == Color.BLACK else Color.BLACK

    
    # =========================
    # Transposition table API
    # =========================
    def tt_get(self, key: Optional[int] = None) -> Optional[TTEntry]:
        """Return TTEntry or None. Default key = current zobrist."""
        if key is None:
            key = self.zobrist_key
        return self.tt.get(key)

    def tt_store(self, depth: int, flag: int, value: int,
                best_move: Optional[Tuple[Tuple[int,int],Tuple[int,int]]],
                key: Optional[int] = None) -> None:
        """Store a TTEntry under key (default = current zobrist)."""
        if key is None:
            key = self.zobrist_key
        # Simple replace policy: replace if deeper or not present.
        existing = self.tt.get(key)
        if existing is None or depth >= existing.depth:
            self.tt[key] = TTEntry(depth, flag, value, best_move)


    # =========================
    # Centralized move ordering
    # =========================
    def order_moves(self,
                    moves: List[Tuple[Tuple[int,int], Tuple[int,int]]],
                    ply: int,
                    tt_move: Optional[Tuple[Tuple[int,int],Tuple[int,int]]] = None
                ) -> List[Tuple[Tuple[int,int], Tuple[int,int]]]:
        """
        Order moves using:
        1) TT best move (if present)
        2) MVV-LVA for captures
        3) Killer moves for ply
        4) History heuristic
        Returns a new list of moves ordered descending (best first).
        """
        board = self.game.board
        pv = tt_move
        piece_vals = PIECE_VALUES
        killer_list = self.killer.get(ply, [])
        hist = self.history

        scored: List[Tuple[int, Tuple[Tuple[int,int], Tuple[int,int]]]] = []
        # micro-optimizations: localize lookups
        for mv in moves:
            (r1, c1), (r2, c2) = mv
            s = 0
            # TT best move gets very large boost
            if pv is not None and mv == pv:
                s += 1_000_000_000

            tgt = board[r2][c2]
            # MVV-LVA-ish: prefer captures of high-value victim and low-value attacker
            if tgt is not None:
                victim_val = piece_vals.get(tgt.type, 0)
                attacker = board[r1][c1]
                attacker_val = piece_vals.get(attacker.type, 0) if attacker else 0
                s += 100000 + (victim_val * 100) - attacker_val

            # killer heuristic
            if mv in killer_list:
                s += 5000

            # history heuristic (from_sq,to_sq)
            key = (r1*8 + c1, r2*8 + c2)
            s += hist.get(key, 0)

            scored.append((s, mv))

        # sort descending by score (stable)
        scored.sort(key=lambda x: x[0], reverse=True)
        return [mv for (_, mv) in scored]
    
    
    def generate_all_moves(self, color: Color) -> List[Tuple[Tuple[int,int], Tuple[int,int]]]:
        """
        Generate all valid moves for the given color.
        Returns list of moves in format [((from_row, from_col), (to_row, to_col)), ...]
        """
        moves = []
        
        # Save the current turn
        original_turn = self.game.current_turn
        
        # Temporarily switch to the desired color to get its moves
        self.game.current_turn = color
        valid_moves_dict = self.game.calculate_moves_fast()

        # Restore the original turn
        self.game.current_turn = original_turn

        # Convert the moves to our format
        for (from_row, from_col), move_list in valid_moves_dict.items():
            for (to_row, to_col) in move_list:
                moves.append(((from_row, from_col), (to_row, to_col)))
        
        return moves

    def is_terminal(self) -> Optional[int]:
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
                p = self.game.board[r][c]
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

    def evaluate_board(self) -> float:
        """
        Returns centipawn evaluation from White's POV.
        Components:
        - Material (base + absorbed abilities)
        - Synergy bonus (multi-ability piece)
        - Mobility (small weight)
        - Castling rights bonus (if already castled still give the bonus)
        """
        score = 0

        # Material & synergy
        for r in range(8):
            for c in range(8):
                p = self.game.board[r][c]
                if not p:
                    continue
                uniq = set(p.abilities)
                val = 0
                for ab in uniq:
                    if (ab == PieceType.BISHOP or ab == PieceType.ROOK) and PieceType.QUEEN in uniq:
                        continue  # avoid double counting major pieces
                    if (ab == PieceType.PAWN and (PieceType.BISHOP in uniq  or PieceType.QUEEN in uniq)):
                        continue  # avoid double counting minor pieces if major present
                    val += PIECE_VALUES.get(ab, 0)
                if p.type not in uniq:
                    val += PIECE_VALUES[p.type]
                ability_count = len(uniq)
                if ability_count >= 2:
                    synergy = int(120 * (ability_count - 1))  # slightly higher synergy
                    val += synergy
                score += val if p.color == Color.WHITE else -val

        # Castling rights bonus (if not already castled)
        def castling_bonus(color):
            bonus = 0
            if(self.game.king_castled[color]):
                return 120
            king_row = 7 if color == Color.WHITE else 0
            king = self.game.get_piece_at(king_row, 4)
            if not king or king.type != PieceType.KING:
                return 0
            # King and rook(s) have not moved
            if not king.has_moved:
                # Kingside
                rook = self.game.get_piece_at(king_row, 7)
                if rook and rook.type == PieceType.ROOK and not rook.has_moved and rook.color == color:
                    bonus += 60
                # Queenside
                rook = self.game.get_piece_at(king_row, 0)
                if rook and rook.type == PieceType.ROOK and not rook.has_moved and rook.color == color:
                    bonus += 60
            return bonus

        score += castling_bonus(Color.WHITE)
        score -= castling_bonus(Color.BLACK)

        # Mobility - calculate legal move count for each color (always from White's perspective)
        original_turn = self.game.current_turn
        self.game.current_turn = Color.WHITE
        white_moves = len(self.game.calculate_moves_fast())
        self.game.current_turn = Color.BLACK
        black_moves = len(self.game.calculate_moves_fast())
        self.game.current_turn = original_turn
        mobility_weight = 6  # centipawns per legal move difference
        score += (white_moves - black_moves) * mobility_weight
        return int(score)

    # -------------------------
    # Enhanced quiescence search
    # -------------------------
    def quiescence(self, alpha: int, beta: int) -> int:
        stand_pat = self.evaluate_board()
        if stand_pat >= beta:
            return beta
        if alpha < stand_pat:
            alpha = stand_pat

        # Only consider captures that pass a cheap SEE heuristic or are promotions (if you have)
        moves = self.generate_all_moves(self.game.current_turn)
        cap_moves = []
        for mv in moves:
            (r1,c1),(r2,c2) = mv
            if self.game.board[r2][c2]:  # capture
                # cheap filter: only include captures with victim_value - attacker_value + stand_pat > alpha- margin
                if self.see_capture((r1,c1),(r2,c2)) + stand_pat >= alpha - 10:
                    cap_moves.append(mv)

        # order captures MVV-LVA (victim value high -> try first)
        cap_moves.sort(key=lambda mv: PIECE_VALUES.get(self.game.board[mv[1][0]][mv[1][1]].type,0), reverse=True)

        for mv in cap_moves:
            state = self.apply_move(mv)
            score = -self.quiescence(-beta, -alpha)
            self.undo_move(state)
            if score >= beta:
                return beta
            if score > alpha:
                alpha = score
        return alpha
    # ------------------------------
    # Minimax with alpha-beta, TT, move ordering, killer/history, quiescence
    # ------------------------------
    def minimax(self, depth: int, alpha: int, beta: int, maximizing_player: bool, ply: int = 0) -> Tuple[int, Optional[Tuple[Tuple[int,int],Tuple[int,int]]]]:
        # TT lookup
        tt_entry = self.tt.get(self.zobrist_key)
        if tt_entry and tt_entry.depth >= depth:
            if tt_entry.flag == EXACT:
                return tt_entry.value, tt_entry.best_move
            elif tt_entry.flag == LOWERBOUND:
                alpha = max(alpha, tt_entry.value)
            elif tt_entry.flag == UPPERBOUND:
                beta = min(beta, tt_entry.value)
            if alpha >= beta:
                return tt_entry.value, tt_entry.best_move

        # Terminal test
        term = self.is_terminal()
        if term is not None:
            return term, None

        if depth == 0:
            # use quiescence instead of raw eval
            val = self.quiescence(alpha, beta)
            return val, None

        # generate moves
        moves = self.generate_all_moves(self.game.current_turn)
        if not moves:
            return self.evaluate_board(), None

        # move ordering:
        def score_move(mv):
            (r1,c1),(r2,c2) = mv
            tgt = self.game.board[r2][c2]
            score = 0
            # captures: MVV-LVA: victim_value * 1000 - attacker_value
            if tgt:
                score += 100000 + PIECE_VALUES.get(tgt.type, 0) * 100 - PIECE_VALUES.get(self.game.board[r1][c1].type, 0)
            # killer moves
            km_list = self.killer.get(ply, [])
            if mv in km_list:
                score += 5000
            # history heuristic
            key = (r1*8+c1, r2*8+c2)
            score += self.history.get(key, 0)
            return score

        moves.sort(key=score_move, reverse=True)

        best_move = None
        original_alpha = alpha

        if maximizing_player:
            max_eval = -math.inf
            for mv in moves:
                state = self.apply_move(mv)
                val, _ = self.minimax(depth - 1, alpha, beta, False, ply+1)
                self.undo_move(state)
                if val > max_eval:
                    max_eval = val
                    best_move = mv
                if val > alpha:
                    alpha = val
                # store history on cutoff
                if alpha >= beta:
                    # killer heuristic: add move to killer for this ply
                    km = self.killer.get(ply, [])
                    if mv not in km:
                        km.insert(0, mv)
                        if len(km) > 2:
                            km.pop()
                        self.killer[ply] = km
                    # history increase
                    key = (mv[0][0]*8+mv[0][1], mv[1][0]*8+mv[1][1])
                    self.history[key] = self.history.get(key, 0) + (1 << depth)
                    break
            value = int(max_eval)
        else:
            min_eval = math.inf
            for mv in moves:
                state = self.apply_move(mv)
                val, _ = self.minimax(depth - 1, alpha, beta, True, ply+1)
                self.undo_move(state)
                if val < min_eval:
                    min_eval = val
                    best_move = mv
                if val < beta:
                    beta = val
                if alpha >= beta:
                    km = self.killer.get(ply, [])
                    if mv not in km:
                        km.insert(0, mv)
                        if len(km) > 2:
                            km.pop()
                        self.killer[ply] = km
                    key = (mv[0][0]*8+mv[0][1], mv[1][0]*8+mv[1][1])
                    self.history[key] = self.history.get(key, 0) + (1 << depth)
                    break
            value = int(min_eval)

        # TT store
        flag = EXACT
        if value <= original_alpha:
            flag = UPPERBOUND
        elif value >= beta:
            flag = LOWERBOUND
        else:
            flag = EXACT
        self.tt[self.zobrist_key] = TTEntry(depth, flag, value, best_move)

        return value, best_move
    
    def is_in_check(self, color: Color) -> bool:
        """Return True if 'color' is in check. Uses generate moves of opponent and checks for capture of king."""
        # find king square
        kr = kc = None
        for r in range(8):
            for c in range(8):
                p = self.game.board[r][c]
                if p and p.type == PieceType.KING and p.color == color:
                    kr, kc = r, c
                    break
            if kr is not None:
                break
        if kr is None:
            return False
        # Opponent moves - if any move targets king square => in check
        orig_turn = self.game.current_turn
        self.game.current_turn = Color.WHITE if color == Color.BLACK else Color.BLACK
        opp_moves = self.game.calculate_moves_fast()
        self.game.current_turn = orig_turn
        # opp_moves is dict: keys (from_row,from_col) -> list of (to_row,to_col)
        for _, mvlist in opp_moves.items():
            for (tr, tc) in mvlist:
                if tr == kr and tc == kc:
                    return True
        return False

    def see_capture(self, from_sq: Tuple[int,int], to_sq: Tuple[int,int]) -> int:
        """
        Cheap SEE approximation (victim_value - attacker_value).
        Returns positive if capture is likely good.
        This is intentionally simple and fast.
        """
        ar, ac = from_sq
        tr, tc = to_sq
        attacker = self.game.board[ar][ac]
        victim = self.game.board[tr][tc]
        if not victim:
            return 0
        attacker_val = PIECE_VALUES.get(attacker.type, 0)
        victim_val = PIECE_VALUES.get(victim.type, 0)
        return victim_val - attacker_val

    

    # ---------------------------------------
    # Negamax with PVS, Null-move, LMR, TT
    # ---------------------------------------
    def negamax(self, depth: int, alpha: int, beta: int, allow_null: bool = True) -> Tuple[Optional[Tuple[Tuple[int,int],Tuple[int,int]]], int]:
        self.node_count += 1
        original_alpha = alpha

        # Transposition table probe
        entry = self.transposition_table.lookup(self.zobrist_key)
        if entry and entry.depth >= depth:
            if entry.flag == "EXACT":
                return entry.best_move, entry.value
            elif entry.flag == "LOWERBOUND":
                alpha = max(alpha, entry.value)
            elif entry.flag == "UPPERBOUND":
                beta = min(beta, entry.value)
            if alpha >= beta:
                return entry.best_move, entry.value

        # Terminal / quiescence
        if depth == 0 or self.game.game_over:
            return None, self.quiescence(alpha, beta)

        color = 1 if self.game.current_turn == Color.WHITE else -1

        # Futility pruning
        if depth == 1:
            static_eval = self.evaluate_board() * color
            margin = 150
            if static_eval + margin <= alpha:
                return None, static_eval

        # Move generation
        moves = self.generate_all_moves(self.game.current_turn)
        # print(f"[ENGINE DEBUG] Generated {len(moves)} moves for {self.game.current_turn}")
        if not moves:
            print(f"[ENGINE DEBUG] No moves available for {self.game.current_turn}")
            return None, self.evaluate_board() * color

        ordered_moves = self.order_moves(moves, depth, entry.best_move if entry else None)

        best_value = -INF
        best_move = None

        for move in ordered_moves:
            state = self.apply_move(move)
            try:
                _, val = self.negamax(depth - 1, -beta, -alpha, allow_null)
                val = -val  # Negate for negamax
                self.undo_move(state)

                if val > best_value:
                    best_value = val
                    best_move = move

                alpha = max(alpha, val)
                if alpha >= beta:
                    break
            except Exception as e:
                # print(f"[ENGINE DEBUG] Error in recursive negamax call: {e}")
                self.undo_move(state)
                raise

        # Store in TT
        flag = "EXACT"
        if best_value <= original_alpha:
            flag = "UPPERBOUND"
        elif best_value >= beta:
            flag = "LOWERBOUND"

        self.transposition_table.store(self.zobrist_key, best_value, depth, flag, best_move)

        #if depth == self.root_depth:
        #    self.best_move = best_move

        return best_move,best_value

    # find_best_move using advanced negamax algorithm
    def find_best_move(self, depth: int = 4) -> Tuple[Tuple[Tuple[int, int], Tuple[int, int]], int]:
        best_move = None
        best_eval = -INF
        alpha = -INF
        beta = INF

        # Aspiration window parameters
        WINDOW = 50  # centipawns

        for d in range(1, depth + 1):
            if best_eval != -INF:
                alpha = max(-INF, best_eval - WINDOW)
                beta = min(INF, best_eval + WINDOW)

            while True:  # re-search if window fails
                current_move, eval_score = self.negamax(d, alpha, beta, True)

                if eval_score <= alpha:  # fail low, widen window down
                    alpha = max(-INF, alpha - 2 * WINDOW)
                    continue
                elif eval_score >= beta:  # fail high, widen window up
                    beta = min(INF, beta + 2 * WINDOW)
                    continue
                else:
                    best_eval = eval_score
                    best_move = current_move
                    break

            print(f"[ENGINE] Depth {d} completed. Best move: {best_move}, Eval: {best_eval}")

        return best_move, best_eval

    def find_best_move_with_time_limit(self, max_depth: int = 4, max_time: float = 5.0) -> Tuple[Optional[Tuple[Tuple[int,int],Tuple[int,int]]], int, int]:
        """
        Find best move with time limit using iterative deepening and advanced negamax.
        
        Args:
            max_depth: Maximum search depth
            max_time: Maximum time in seconds
            
        Returns:
            (best_move, eval_score, actual_depth_reached)
        """
        start_time = time.time()
        best_move = None
        best_eval = -INF
        alpha = -INF
        beta = INF
        
        # Clear search tables for fresh start
        self.killer.clear()
        
        # Iterative deepening with negamax
        for depth in range(1, max_depth + 1):
            if time.time() - start_time > max_time:
                print(f"[ENGINE] Time limit reached at depth {depth-1}")
                break
                
            try:
                current_move, current_eval = self.negamax(depth, -10**9, 10**9)
                
                # Convert to White's perspective
                if self.game.current_turn == Color.BLACK:
                    current_eval = -current_eval
                
                if current_move:
                    best_move = current_move
                    best_eval = current_eval
                    depth_reached = depth
                    elapsed = time.time() - start_time
                    print(f"[ENGINE] Depth {depth}: {current_move} (eval: {current_eval}, time: {elapsed:.2f}s)")
            except KeyboardInterrupt:
                print(f"[ENGINE] Search interrupted at depth {depth}")
                break
                
        total_time = time.time() - start_time
        print(f"[ENGINE] Advanced search completed: depth {depth_reached}, time {total_time:.2f}s")
        print(f"[ENGINE] Transposition table entries: {len(self.tt)}")
        return best_move, best_eval, depth_reached
