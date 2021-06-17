import chess.pgn
import argparse
import glob
import re
from typing import List

# todo close in c++ tools using pgn-extract
# https://www.cs.kent.ac.uk/people/staff/djb/pgn-extract/help.html#-w

commentRe = re.compile("([+-]*M*[0-9.]*)/([0-9]*)")
mateRe = re.compile("([+-])M([0-9]*)")
flip_black = False

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
        print("illegal result", result_str)
        raise ValueError

def game_sanity_check(game: chess.pgn.Game) -> bool:
    if not game.headers["Result"] in ["1/2-1/2", "0-1", "1-0"]:
        print("invalid result", game.headers["Result"])
        return False
    return True

def parse_comment_for_score(comment_str: str, board: chess.Board) -> int:
    global commentRe
    global mateRe
    global flip_black

    try:
      m = commentRe.search(comment_str)
      if m:
         score = m.group(1)
         # depth = int(m.group(2))
         m = mateRe.search(score)
         if m:
            if m.group(1) == "+":
               score =  32000 - int(m.group(2))
            else:
               score = -32000 + int(m.group(2))
         else:
            score = int(float(score) * 208) # pawn to SF PawnValueEg

         if flip_black and board.turn == chess.BLACK:
            score = -score
      else:
         score = 0
    except:
      score = 0

    return score

def parse_game(game: chess.pgn.Game, writer, start_play: int=1)->None:
    board: chess.Board = game.board()
    if not game_sanity_check(game):
        return

    result: str = game.headers["Result"]
    ply = 0
    for node in game.mainline():
        move = node.move
        if ply >= start_play:
            comment: str = node.comment
            writer.write("fen " + board.fen() + "\n")
            writer.write("move " + str(move) + "\n")
            writer.write("score " + str(parse_comment_for_score(comment, board)) + "\n")
            writer.write("ply " + str(ply)+"\n")
            writer.write("result " + str(parse_result(result, board)) +"\n")
            writer.write("e\n")
        ply += 1
        board.push(move)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pgn", type=str, required=True)
    parser.add_argument("--start_ply", type=int, default=1)
    parser.add_argument("--output", type=str, default="plain.txt")
    parser.add_argument("--flip_black_score", action='store_true', dest='flip_black_score', help="Flip black score. Default: False")
    args = parser.parse_args()

    global flip_black
    flip_black = args.flip_black_score

    pgn_files: List[str] = glob.glob(args.pgn)
    pgn_files = sorted(pgn_files, key=lambda x:float(re.findall("-(\d+).pgn",x)[0] if re.findall("-(\d+).pgn",x) else 0.0))
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
