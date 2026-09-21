# OWNERSHIP=Ascanius
import tkinter as tk
from tkinter import messagebox, simpledialog, Toplevel, Button
import chess
import chess.svg
import cairosvg
from PIL import Image, ImageTk
import io
import threading
import sys
import os
import re
from queue import Empty, Queue
sys.stdout = open(os.devnull, 'w')
import pygame
sys.stdout = sys.__stdout__
pygame.init()
# Load sound effects from chess.com
move_sound = pygame.mixer.Sound('utilitys/move-self.mp3')
capture_sound = pygame.mixer.Sound('utilitys/capture.mp3')
check_sound = pygame.mixer.Sound('utilitys/move-check.mp3')
castle_sound = pygame.mixer.Sound('utilitys/castle.mp3')
promote_sound = pygame.mixer.Sound('utilitys/promote.mp3')

import time

HOVER_DELAY_MS = 100
PREVIEW_SIZE = 220
_MOVE_NUM_RE = re.compile(r"^\d+\.+$")
_RESULT_RE = re.compile(r"^(1-0|0-1|1/2-1/2)$")

# Mirrors interpret_eval() in ascaniusfish.hpp and max_mating_seq in lib/Settings.hpp:
# mate scores are encoded near INT_MIN/INT_MAX; distance-to-mate is eval-INT_MIN or INT_MAX-eval.
_INT_MIN = -2147483648
_INT_MAX = 2147483647
_MAX_MATING_SEQ = 1000


def render_board_photo(board, size, flipped=False):
    svg_data = chess.svg.board(board, size=size, flipped=flipped).encode("utf-8")
    png_data = cairosvg.svg2png(bytestring=svg_data)
    image = Image.open(io.BytesIO(png_data))
    return ImageTk.PhotoImage(image)


def format_eval(eval_cp):
    if eval_cp <= _INT_MIN + _MAX_MATING_SEQ:
        return f"#-{eval_cp - _INT_MIN}"
    if eval_cp >= _INT_MAX - _MAX_MATING_SEQ:
        return f"#+{_INT_MAX - eval_cp}"
    return f"{eval_cp / 100:.2f}"


class parameters:
    volume = 100



def write_to_last_move_file(move, colour):
    with open('last_move.txt', 'w') as file:
        file.write(move)
        if colour == 0:
            file.write('ww')  # wait wait (two times, to compensate for the possibility of promotion)
        else:
            file.write('bb')  # go go (two times, to compensate for the possibility of promotion)

my_cpp_program = "a.out"

game_parameters = parameters()
class ChessApp:
    
    def __init__(self, root, fen=None):
        self.root = root
        self.root.title("Chess Board")
        self.board_flipped = False  # Track board orientation
        
        # Initialize the board
        if fen:
            try:
                self.board = chess.Board(fen)
            except ValueError:
                messagebox.showerror("Error", "Invalid FEN string. Initializing to default position.")
                self.board = chess.Board()
        else:
            fen = simpledialog.askstring("Input", "Enter the FEN string for the initial board setup (or leave empty for default):")
            if fen:
                try:
                    self.board = chess.Board(fen)
                except ValueError:
                    messagebox.showerror("Error", "Invalid FEN string. Initializing to default position.")
                    self.board = chess.Board()
            else:
                self.board = chess.Board()

        self.board_image = None
        self.image_size = 400  # Initial size

        root.grid_rowconfigure(0, weight=9)
        root.grid_rowconfigure(1, weight=1)
        root.grid_columnconfigure(0, weight=3)
        root.grid_columnconfigure(1, weight=1)

        self.canvas = tk.Canvas(root, width=self.image_size, height=self.image_size)
        self.canvas.grid(row=0, column=0, sticky="nsew")

        # Predesignated preview-board area: top-left of the free space to the right of the main board,
        # hugging the main board (minimal left padding) rather than centered in its column.
        self.preview_canvas = tk.Canvas(root, width=PREVIEW_SIZE, height=PREVIEW_SIZE, highlightthickness=0)
        self.preview_canvas.grid(row=0, column=1, sticky="nw", padx=(4, 12), pady=8)

        self.engine_line_text = tk.Text(
            root, height=4, wrap="word", state="disabled",
            borderwidth=0, highlightthickness=0, cursor="arrow"
        )
        self.engine_line_text.grid(row=1, column=0, sticky="nsew", padx=8, pady=4)
        self.engine_line_text.insert("1.0", "Engine line: ")
        self.engine_line_text.config(state="disabled")

        self.pv_base_board = None      # chess.Board snapshot at PV-parse time
        self.pv_moves = []             # SAN tokens, index == hover index
        self._hover_after_id = None    # pending "show after delay" timer
        self._leave_after_id = None    # pending "confirm real leave" timer (see _on_move_leave)
        self._in_hover_mode = False    # once True, switching between moves is instant
        self._preview_photo = None     # keep a live ref so Tk doesn't GC it

        self.update_board_image()

        self.canvas.bind("<Button-1>", self.on_click)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_drop)
        self.canvas.bind("<Button-3>", self.on_right_click)
        self.selected_square = None
        self.drag_data = {"x": 0, "y": 0, "item": None}

        self.canvas.bind("<Configure>", self.on_resize)
        
        # Variables to track volume adjustments
        self.is_volume_adjusting = False
        self.volume_input = ""
        
        # Bind 'f' key to flip the board
        self.root.bind("f", self.flip_board)
        self.root.bind("F", self.flip_board)
 # Bind 'm' key to start volume adjustment and detect release
        self.root.bind("<KeyPress-m>", self.start_volume_adjustment)
        self.root.bind("<KeyRelease-m>", self.finalize_volume_adjustment)

        # Bind keys for numeric input
        for digit in range(10):
            self.root.bind(str(digit), self.capture_volume_digit)

        # Bind Enter key to finalize the volume input
        self.root.bind("<Return>", self.finalize_volume_adjustment)


        self.command_queue = Queue()
        self.command_thread = threading.Thread(target=self.command_line_input)
        self.command_thread.daemon = True
        self.command_thread.start()
        self.root.after(50, self.process_command_queue)

   
    def start_volume_adjustment(self, event):
        """Handles pressing 'm' for mute/unmute or to start volume adjustment."""
        if not self.is_volume_adjusting:  # Start adjusting volume
            self.is_volume_adjusting = True
            self.volume_input = ""
            print("Volume adjustment started. Enter a number (0-100) while holding 'm'.")

    def capture_volume_digit(self, event):
        """Handles numeric input for volume while 'm' is pressed."""
        if self.is_volume_adjusting:
            self.volume_input += event.char
            print(f"Current volume input: {self.volume_input}")

    def finalize_volume_adjustment(self, event):
        """Finalizes volume adjustment when 'm' is released."""
        if self.is_volume_adjusting:
            if self.volume_input == "":  # No input, toggle mute/unmute
                self.toggle_mute()
            elif self.volume_input.isdigit():  # Valid numeric input
                volume = int(self.volume_input)
                if 0 <= volume <= 100:
                    self.adjust_volume(volume, event)
                else:
                    pass
            self.is_volume_adjusting = False
            self.volume_input = ""

    def toggle_mute(self):
        """Toggles mute/unmute functionality."""
        current_volume = move_sound.get_volume()
        if current_volume > 0:
    
            self.adjust_volume(0, None)  # Mute
        else:

            self.adjust_volume(game_parameters.volume, None)  # Unmute

    def adjust_volume(self, volume, event):
        game_parameters.volume
        if(volume!=0):
            game_parameters.volume = volume
        
        
        
        move_sound.set_volume(volume/100)
        capture_sound.set_volume(volume/100)
        check_sound.set_volume(volume/100)
        castle_sound.set_volume(volume/100)
        promote_sound.set_volume(volume/100)    

    def update_board_image(self):
        self.board_image = render_board_photo(self.board, self.image_size, self.board_flipped)

        self.canvas.create_image(0, 0, anchor=tk.NW, image=self.board_image)
        self.canvas.config(scrollregion=self.canvas.bbox(tk.ALL))

        # Determine the actual size of the rendered board including borders
        self.board_actual_size = self.board_image.width()  # assumes square image
        self.label_size = int(1/30 * self.board_actual_size)  # 1/30 of the board size
        self.square_size = (self.image_size - self.label_size * 2) // 8  # Calculate square size

    def flip_board(self, event):
        self.board_flipped = not self.board_flipped
        self.update_board_image()

    def get_square_under_mouse(self, event):
        col = (event.x - self.label_size) // self.square_size
        row = 7 - ((event.y - self.label_size) // self.square_size)

        if self.board_flipped:
            col = 7 - col
            row = 7 - row

        if 0 <= col < 8 and 0 <= row < 8:
            return chess.square(col, row)
        return None

    def on_click(self, event):
        square = self.get_square_under_mouse(event)
        if square is not None:
            if self.selected_square is None:
                self.selected_square = square
                self.drag_data["item"] = self.highlight_square(square)
                self.drag_data["x"] = event.x
                self.drag_data["y"] = event.y
            else:
                self.make_move(square)
                self.selected_square = None

    def on_drag(self, event):
        if self.drag_data["item"]:
            dx = event.x - self.drag_data["x"]
            dy = event.y - self.drag_data["y"]
            self.canvas.move(self.drag_data["item"], dx, dy)
            self.drag_data["x"] = event.x
            self.drag_data["y"] = event.y

    def on_right_click(self, event):
        self.selected_square = None
        self.update_board_image()

    def on_drop(self, event):
        if self.drag_data["item"]:
            square = self.get_square_under_mouse(event)
            if square is not None:
                if self.selected_square is not None and self.selected_square == square:
                    self.drag_data["item"] = None
                    self.update_board_image()
                    self.highlight_square(square)
                else:
                    self.make_move(square)
                    self.canvas.delete(self.drag_data["item"])
                    self.drag_data["item"] = None
                    self.selected_square = None
                    self.update_board_image()

    def play_movesound(self, move):
        if self.board.gives_check(move):
            check_sound.play()
            
        elif self.board.is_castling(move):
            castle_sound.play()
            
        elif self.board.is_capture(move):
            capture_sound.play()
            
        else:
            move_sound.play()

    def make_move(self, square, promotion_type=None):
        move = chess.Move(self.selected_square, square)
        piece = self.board.piece_at(self.selected_square)

        # Check if it's a pawn promotion
        if piece and piece.piece_type == chess.PAWN and chess.square_rank(square) in [0, 7]:
            promotion_moves = [
                chess.Move(self.selected_square, square, promotion=promo_piece)
                for promo_piece in [chess.QUEEN, chess.ROOK, chess.BISHOP, chess.KNIGHT]
            ]
            legal_promotion_moves = [m for m in promotion_moves if m in self.board.legal_moves]
            if legal_promotion_moves and promotion_type is None:
                self.show_promotion_dialog(move)
                promote_sound.play()
            elif promotion_type:
                self.promote(move, promotion_type)
                promote_sound.play()
            else:
                pass
        else:
            if move in self.board.legal_moves:
                
                self.play_movesound(move)
                    
                self.board.push(move)
                self.update_board_image()
                
                
                write_to_last_move_file(f"{move.uci()}", self.board.turn)
                if self.board.is_game_over():
                    messagebox.showinfo("Game Over", self.board.result())
            else:
                pass

    def promote(self, move, piece):
        move.promotion = piece
        self.board.push(move)
        self.update_board_image()
        print(f"{move.uci()}")
        write_to_last_move_file(f"{move.uci()}", self.board.turn)
        if self.board.is_game_over():
            messagebox.showinfo("Game Over", self.board.result())

    def promote_and_close(self, window, move, piece_type):
        self.promote(move, piece_type)
        window.destroy()
    def show_promotion_dialog(self, move):
        
        
        promotion_window = Toplevel(self.root)
        promotion_window.title("Choose promotion piece")
        promotion_window.geometry("300x200")

        Button(promotion_window, text="Queen", command=lambda: self.promote_and_close(promotion_window, move, chess.QUEEN)).pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        Button(promotion_window, text="Rook", command=lambda: self.promote_and_close(promotion_window, move, chess.ROOK)).pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        Button(promotion_window, text="Bishop", command=lambda: self.promote_and_close(promotion_window, move, chess.BISHOP)).pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        Button(promotion_window, text="Knight", command=lambda: self.promote_and_close(promotion_window, move, chess.KNIGHT)).pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        

    def highlight_square(self, square):
        col = chess.square_file(square)
        row = chess.square_rank(square)
        if self.board_flipped:
            col = 7 - col
            row = 7 - row

        x1 = col * self.square_size + self.label_size
        y1 = (7 - row) * self.square_size + self.label_size
        x2 = x1 + self.square_size
        y2 = y1 + self.square_size
        return self.canvas.create_rectangle(x1, y1, x2, y2, outline="green", width=3)

    def on_resize(self, event):
        # Bound to self.canvas (not root), so event.width/height already reflect
        # the board's own grid cell, independent of the side preview panel's column.
        self.image_size = max(160, min(event.width, event.height))
        self.update_board_image()

    def update_engine_line(self, line):
        self._clear_preview()
        if self._hover_after_id:
            self.root.after_cancel(self._hover_after_id)
            self._hover_after_id = None
        if self._leave_after_id:
            self.root.after_cancel(self._leave_after_id)
            self._leave_after_id = None
        self._in_hover_mode = False

        match = re.match(r"depth=(\d+)\s+eval=(-?\d+)\s*(.*)", line)
        self.engine_line_text.config(state="normal")
        self.engine_line_text.delete("1.0", tk.END)

        if not match:
            self.engine_line_text.insert(tk.END, "Engine line: " + line)
            self.engine_line_text.config(state="disabled")
            return

        depth = int(match.group(1))
        eval_str = format_eval(int(match.group(2)))
        pgn_text = match.group(3).strip()

        self.pv_base_board = self.board.copy()  # snapshot now, before any further moves
        self.pv_moves = []

        self.engine_line_text.insert(tk.END, f"Depth: {depth}    Evaluation: {eval_str}\nEngine line: ")

        # Non-hoverable tokens (move-number labels) are folded into the tag of the NEXT
        # hoverable move, so every character from the first move onward belongs to some
        # move's tag - hitboxes touch exactly, with no untagged gap to hover through.
        pending_prefix = ""
        for token in pgn_text.split():
            if token.startswith("["):
                break
            if _MOVE_NUM_RE.match(token) or _RESULT_RE.match(token):
                pending_prefix += token + " "
                continue
            idx = len(self.pv_moves)
            self.pv_moves.append(token)
            tag = f"pvmove{idx}"
            self.engine_line_text.insert(tk.END, pending_prefix + token + " ", tag)
            pending_prefix = ""
            self.engine_line_text.tag_bind(tag, "<Enter>", lambda e, i=idx: self._on_move_enter(e, i))
            self.engine_line_text.tag_bind(tag, "<Leave>", lambda e, i=idx: self._on_move_leave(e, i))
        if pending_prefix:
            self.engine_line_text.insert(tk.END, pending_prefix)

        self.engine_line_text.config(state="disabled")

    def _on_move_enter(self, event, index):
        if self._leave_after_id:
            # An adjacent move's <Leave> just fired (touching hitboxes -> same event
            # dispatch); cancel its tentative "exit hover mode" so we stay in hover mode.
            self.root.after_cancel(self._leave_after_id)
            self._leave_after_id = None
        if self._hover_after_id:
            self.root.after_cancel(self._hover_after_id)
            self._hover_after_id = None
        self._clear_preview()  # the previously hovered move's preview vanishes immediately
        if self._in_hover_mode:
            self._show_preview(index)  # already hovering -> switch instantly, no delay
        else:
            self._hover_after_id = self.root.after(HOVER_DELAY_MS, lambda: self._show_preview(index))

    def _on_move_leave(self, event, index):
        if self._hover_after_id:
            self.root.after_cancel(self._hover_after_id)
            self._hover_after_id = None
        self._clear_preview()
        # Don't drop out of hover mode synchronously: if this Leave is immediately
        # followed by the next move's Enter (touching hitboxes fire Leave then Enter
        # for the same mouse-move event), that Enter will cancel this and we stay
        # in hover mode. Only a real move-away-from-all-moves confirms the exit.
        if self._leave_after_id:
            self.root.after_cancel(self._leave_after_id)
        self._leave_after_id = self.root.after_idle(self._confirm_leave)

    def _confirm_leave(self):
        self._leave_after_id = None
        self._in_hover_mode = False

    def _show_preview(self, index):
        self._hover_after_id = None
        if self.pv_base_board is None or index >= len(self.pv_moves):
            return
        preview_board = self.pv_base_board.copy()
        try:
            for san in self.pv_moves[:index + 1]:
                preview_board.push_san(san)
        except ValueError:
            return  # defensive only; shouldn't happen given the base-board snapshot
        self._in_hover_mode = True

        self._preview_photo = render_board_photo(preview_board, PREVIEW_SIZE, self.board_flipped)
        self.preview_canvas.delete("all")
        self.preview_canvas.create_image(0, 0, anchor=tk.NW, image=self._preview_photo)

    def _clear_preview(self):
        self.preview_canvas.delete("all")
        self._preview_photo = None

    def process_command_queue(self):
        try:
            while True:
                move = self.command_queue.get_nowait()
                try:
                    self.handle_command(move)
                except Exception as error:
                    print(f"Error handling command {move!r}: {error}", file=sys.stderr)
        except Empty:
            pass
        self.root.after(50, self.process_command_queue)

    def handle_command(self, move):
        if move.startswith("PV "):
            self.update_engine_line(move[3:].strip())
            return

        if move == "exit()":
            print("Exiting the program.")
            self.root.quit()
            return

        try:
            uci_move = chess.Move.from_uci(move)
            piece = self.board.piece_at(uci_move.from_square)

            if piece and piece.piece_type == chess.PAWN and chess.square_rank(uci_move.to_square) in [0, 7]:
                promotion_moves = [
                    chess.Move(uci_move.from_square, uci_move.to_square, promotion=promo_piece)
                    for promo_piece in [chess.QUEEN, chess.ROOK, chess.BISHOP, chess.KNIGHT]
                ]
                legal_promotion_moves = [m for m in promotion_moves if m in self.board.legal_moves]
                if legal_promotion_moves and len(move) == 4:
                    self.show_promotion_dialog(uci_move)
                elif legal_promotion_moves and len(move) == 5:
                    if move[4] == 'q':
                        uci_move.promotion = chess.QUEEN
                    if move[4] == 'r':
                        uci_move.promotion = chess.ROOK
                    if move[4] == 'b':
                        uci_move.promotion = chess.BISHOP
                    if move[4] == 'n':
                        uci_move.promotion = chess.KNIGHT
                    self.board.push(uci_move)
                    self.update_board_image()
                    print(f"{uci_move.uci()}")
                    write_to_last_move_file(f"{uci_move.uci()}", self.board.turn)
                    if self.board.is_game_over():
                        messagebox.showinfo("Game Over", self.board.result())
            elif uci_move in self.board.legal_moves:
                self.board.push(uci_move)
                self.update_board_image()
                print(f"{uci_move.uci()}")
                write_to_last_move_file(f"{uci_move.uci()}", self.board.turn)
                if self.board.is_game_over():
                    print("Game Over:", self.board.result())
        except ValueError:
            pass

    def command_line_input(self):
        while True:
            try:
                move = input("").strip()
            except EOFError:
                return
            self.command_queue.put(move)

if __name__ == "__main__":
    fen_argument = sys.argv[1] if len(sys.argv) > 1 else None
    root = tk.Tk()
    app = ChessApp(root, fen=fen_argument)
    root.mainloop()
