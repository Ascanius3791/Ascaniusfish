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

        self.canvas = tk.Canvas(root, width=self.image_size, height=self.image_size)
        self.canvas.pack(fill=tk.BOTH, expand=True)

        self.update_board_image()

        self.canvas.bind("<Button-1>", self.on_click)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_drop)
        self.canvas.bind("<Button-3>", self.on_right_click) 
        self.selected_square = None
        self.drag_data = {"x": 0, "y": 0, "item": None}

        self.root.bind("<Configure>", self.on_resize)
        
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


        self.command_thread = threading.Thread(target=self.command_line_input)
        self.command_thread.daemon = True
        self.command_thread.start()

   
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
        svg_data = chess.svg.board(self.board, size=self.image_size, flipped=self.board_flipped).encode("utf-8")
        png_data = cairosvg.svg2png(bytestring=svg_data)
        image = Image.open(io.BytesIO(png_data))
        self.board_image = ImageTk.PhotoImage(image)

        self.canvas.create_image(0, 0, anchor=tk.NW, image=self.board_image)
        self.canvas.config(scrollregion=self.canvas.bbox(tk.ALL))

        # Determine the actual size of the rendered board including borders
        self.board_actual_size = image.width  # assumes square image
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
        new_size = min(event.width, event.height)
        self.image_size = new_size - 2 * self.label_size
        self.square_size = (self.image_size - self.label_size * 2) // 8
        self.update_board_image()

    def command_line_input(self):
        while not self.board.is_game_over():
            move = input("").strip()
            
            if move == "exit()":
                print("Exiting the program.")
                self.root.quit()
                sys.exit()
                
            try:
                uci_move = chess.Move.from_uci(move)
                piece = self.board.piece_at(uci_move.from_square)
                
                # Check if it's a pawn promotion
                if piece and piece.piece_type == chess.PAWN and chess.square_rank(uci_move.to_square) in [0, 7]:
                    promotion_moves = [
                        chess.Move(uci_move.from_square, uci_move.to_square, promotion=promo_piece)
                        for promo_piece in [chess.QUEEN, chess.ROOK, chess.BISHOP, chess.KNIGHT]
                    ]
                    legal_promotion_moves = [m for m in promotion_moves if m in self.board.legal_moves]
                    if legal_promotion_moves and len(move) == 4:
                        self.show_promotion_dialog(uci_move)
                    if legal_promotion_moves and len(move) == 5:
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
                    else:
                        pass
                else:
                    if uci_move in self.board.legal_moves:
                        self.board.push(uci_move)
                        self.update_board_image()
                        print(f"{uci_move.uci()}")
                        write_to_last_move_file(f"{uci_move.uci()}", self.board.turn)
                        if self.board.is_game_over():
                            print("Game Over:", self.board.result())
                    else:
                        pass
            except ValueError:
                pass

if __name__ == "__main__":
    fen_argument = sys.argv[1] if len(sys.argv) > 1 else None
    root = tk.Tk()
    app = ChessApp(root, fen=fen_argument)
    root.mainloop()
