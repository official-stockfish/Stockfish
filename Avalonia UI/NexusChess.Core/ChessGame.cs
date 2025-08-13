using System;
using System.Collections.Generic;
using System.Linq;

namespace NexusChess.Core
{
    public enum PieceType
    {
        None, Pawn, Rook, Knight, Bishop, Queen, King
    }

    public enum PieceColor
    {
        White, Black
    }

    public struct ChessPiece
    {
        public PieceType Type { get; set; }
        public PieceColor Color { get; set; }

        public ChessPiece(PieceType type, PieceColor color)
        {
            Type = type;
            Color = color;
        }

        public bool IsEmpty => Type == PieceType.None;

        public char ToFenChar()
        {
            if (IsEmpty) return ' ';
            
            var piece = Type switch
            {
                PieceType.Pawn => 'p',
                PieceType.Rook => 'r',
                PieceType.Knight => 'n',
                PieceType.Bishop => 'b',
                PieceType.Queen => 'q',
                PieceType.King => 'k',
                _ => ' '
            };

            return Color == PieceColor.White ? char.ToUpper(piece) : piece;
        }

        public string ToUnicodeSymbol()
        {
            if (IsEmpty) return "";

            return (Type, Color) switch
            {
                (PieceType.King, PieceColor.White) => "♔",
                (PieceType.Queen, PieceColor.White) => "♕",
                (PieceType.Rook, PieceColor.White) => "♖",
                (PieceType.Bishop, PieceColor.White) => "♗",
                (PieceType.Knight, PieceColor.White) => "♘",
                (PieceType.Pawn, PieceColor.White) => "♙",
                (PieceType.King, PieceColor.Black) => "♚",
                (PieceType.Queen, PieceColor.Black) => "♛",
                (PieceType.Rook, PieceColor.Black) => "♜",
                (PieceType.Bishop, PieceColor.Black) => "♝",
                (PieceType.Knight, PieceColor.Black) => "♞",
                (PieceType.Pawn, PieceColor.Black) => "♟",
                _ => ""
            };
        }
    }

    public struct Square
    {
        public int File { get; }
        public int Rank { get; }

        public Square(int file, int rank)
        {
            File = file;
            Rank = rank;
        }

        public Square(string notation)
        {
            if (notation.Length != 2)
                throw new ArgumentException("Invalid square notation");

            File = notation[0] - 'a';
            Rank = notation[1] - '1';
        }

        public string ToNotation() => $"{(char)('a' + File)}{Rank + 1}";

        public bool IsValid => File >= 0 && File < 8 && Rank >= 0 && Rank < 8;
    }

    public struct ChessMove
    {
        public Square From { get; set; }
        public Square To { get; set; }
        public PieceType PromotionPiece { get; set; }

        public ChessMove(Square from, Square to, PieceType promotionPiece = PieceType.None)
        {
            From = from;
            To = to;
            PromotionPiece = promotionPiece;
        }

        public string ToUciNotation()
        {
            var move = From.ToNotation() + To.ToNotation();
            if (PromotionPiece != PieceType.None)
            {
                move += PromotionPiece switch
                {
                    PieceType.Queen => "q",
                    PieceType.Rook => "r",
                    PieceType.Bishop => "b",
                    PieceType.Knight => "n",
                    _ => ""
                };
            }
            return move;
        }
    }

    public class ChessGame
    {
        private ChessPiece[,] _board = new ChessPiece[8, 8];
        private List<ChessMove> _moveHistory = new List<ChessMove>();
        private PieceColor _sideToMove = PieceColor.White;
        private bool _whiteCanCastleKingside = true;
        private bool _whiteCanCastleQueenside = true;
        private bool _blackCanCastleKingside = true;
        private bool _blackCanCastleQueenside = true;
        private Square? _enPassantTarget = null;
        private int _halfMoveClock = 0;
        private int _fullMoveNumber = 1;

        public ChessPiece[,] Board => _board;
        public PieceColor SideToMove => _sideToMove;
        public IReadOnlyList<ChessMove> MoveHistory => _moveHistory.AsReadOnly();

        public ChessGame()
        {
            SetupInitialPosition();
        }

        private void SetupInitialPosition()
        {
            // Clear the board
            for (int file = 0; file < 8; file++)
            {
                for (int rank = 0; rank < 8; rank++)
                {
                    _board[file, rank] = new ChessPiece();
                }
            }

            // Set up white pieces
            _board[0, 0] = new ChessPiece(PieceType.Rook, PieceColor.White);
            _board[1, 0] = new ChessPiece(PieceType.Knight, PieceColor.White);
            _board[2, 0] = new ChessPiece(PieceType.Bishop, PieceColor.White);
            _board[3, 0] = new ChessPiece(PieceType.Queen, PieceColor.White);
            _board[4, 0] = new ChessPiece(PieceType.King, PieceColor.White);
            _board[5, 0] = new ChessPiece(PieceType.Bishop, PieceColor.White);
            _board[6, 0] = new ChessPiece(PieceType.Knight, PieceColor.White);
            _board[7, 0] = new ChessPiece(PieceType.Rook, PieceColor.White);

            for (int file = 0; file < 8; file++)
            {
                _board[file, 1] = new ChessPiece(PieceType.Pawn, PieceColor.White);
            }

            // Set up black pieces
            _board[0, 7] = new ChessPiece(PieceType.Rook, PieceColor.Black);
            _board[1, 7] = new ChessPiece(PieceType.Knight, PieceColor.Black);
            _board[2, 7] = new ChessPiece(PieceType.Bishop, PieceColor.Black);
            _board[3, 7] = new ChessPiece(PieceType.Queen, PieceColor.Black);
            _board[4, 7] = new ChessPiece(PieceType.King, PieceColor.Black);
            _board[5, 7] = new ChessPiece(PieceType.Bishop, PieceColor.Black);
            _board[6, 7] = new ChessPiece(PieceType.Knight, PieceColor.Black);
            _board[7, 7] = new ChessPiece(PieceType.Rook, PieceColor.Black);

            for (int file = 0; file < 8; file++)
            {
                _board[file, 6] = new ChessPiece(PieceType.Pawn, PieceColor.Black);
            }
        }

        public ChessPiece GetPiece(Square square)
        {
            if (!square.IsValid) return new ChessPiece();
            return _board[square.File, square.Rank];
        }

        public bool IsValidMove(ChessMove move)
        {
            // Basic validation - piece exists and belongs to the player
            var piece = GetPiece(move.From);
            if (piece.IsEmpty || piece.Color != _sideToMove)
                return false;

            var targetPiece = GetPiece(move.To);
            if (!targetPiece.IsEmpty && targetPiece.Color == _sideToMove)
                return false;

            // TODO: Add full move validation logic
            return true;
        }

        public bool MakeMove(ChessMove move)
        {
            if (!IsValidMove(move))
                return false;

            var piece = GetPiece(move.From);
            _board[move.To.File, move.To.Rank] = piece;
            _board[move.From.File, move.From.Rank] = new ChessPiece();

            _moveHistory.Add(move);
            _sideToMove = _sideToMove == PieceColor.White ? PieceColor.Black : PieceColor.White;
            
            if (_sideToMove == PieceColor.White)
                _fullMoveNumber++;

            return true;
        }

        public string ToFen()
        {
            var fen = "";

            // Board state
            for (int rank = 7; rank >= 0; rank--)
            {
                int emptyCount = 0;
                for (int file = 0; file < 8; file++)
                {
                    var piece = _board[file, rank];
                    if (piece.IsEmpty)
                    {
                        emptyCount++;
                    }
                    else
                    {
                        if (emptyCount > 0)
                        {
                            fen += emptyCount.ToString();
                            emptyCount = 0;
                        }
                        fen += piece.ToFenChar();
                    }
                }
                if (emptyCount > 0)
                    fen += emptyCount.ToString();
                
                if (rank > 0)
                    fen += "/";
            }

            // Side to move
            fen += _sideToMove == PieceColor.White ? " w " : " b ";

            // Castling rights
            var castling = "";
            if (_whiteCanCastleKingside) castling += "K";
            if (_whiteCanCastleQueenside) castling += "Q";
            if (_blackCanCastleKingside) castling += "k";
            if (_blackCanCastleQueenside) castling += "q";
            fen += string.IsNullOrEmpty(castling) ? "- " : castling + " ";

            // En passant target
            fen += _enPassantTarget?.ToNotation() ?? "-";
            fen += " ";

            // Half-move clock and full-move number
            fen += $"{_halfMoveClock} {_fullMoveNumber}";

            return fen;
        }

        public void NewGame()
        {
            _moveHistory.Clear();
            _sideToMove = PieceColor.White;
            _whiteCanCastleKingside = true;
            _whiteCanCastleQueenside = true;
            _blackCanCastleKingside = true;
            _blackCanCastleQueenside = true;
            _enPassantTarget = null;
            _halfMoveClock = 0;
            _fullMoveNumber = 1;
            SetupInitialPosition();
        }
    }
}