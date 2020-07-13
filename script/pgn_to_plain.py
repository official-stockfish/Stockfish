import chess.pgn
import argparse
import glob
from typing import List

# todo close in c++ tools using pgn-extract
# https://www.cs.kent.ac.uk/people/staff/djb/pgn-extract/help.html#-w

def parse_result(result_str:str, board:chess.Board) -> int:
    if result_str == "1/2-1/2":
        return 0
    if result_str == "0-1":
        if board.turn == chess.WHITE:
            return -1
        else:
            return 1
    elif result_str == "1-0":
        if board.turn == chess.WHITE:
            return 1
        else:
            return -1
    else:
        print("illeagal result", result_str)
        raise ValueError

def game_sanity_check(game: chess.pgn.Game) -> bool:
    if not game.headers["Result"] in ["1/2-1/2", "0-1", "1-0"]:
        print("invalid result", game.headers["Result"])
        return False
    return True
    
def parse_game(game: chess.pgn.Game, writer, start_play: int=1)->None:
    board: chess.Board = game.board()
    if not game_sanity_check(game):
        return
    result: str = game.headers["Result"]
    for ply, move in enumerate(game.mainline_moves()):
        if ply >= start_play:
            writer.write("fen " + board.fen() + "\n")
            writer.write("move " + str(move) + "\n")
            writer.write("score 0\n")
            writer.write("ply " + str(ply)+"\n")
            writer.write("result " + str(parse_result(result, board)) +"\n")
            writer.write("e\n")

        board.push(move)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pgn", type=str, required=True)
    parser.add_argument("--start_ply", type=int, default=1)
    parser.add_argument("--output", type=str, default="plain.txt")
    args = parser.parse_args()

    pgn_files: List[str] = glob.glob(args.pgn)
    f = open(args.output, 'w')
    for pgn_file in pgn_files:
        print("parse", pgn_file)
        pgn_loader = open(pgn_file)
        while True:
            game = chess.pgn.read_game(pgn_loader)
            if game is None:
                break
            parse_game(game, f, args.start_ply)
    f.close()
    
if __name__=="__main__":
    main()
