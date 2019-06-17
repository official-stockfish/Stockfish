#if defined (EVAL_LEARN)

#include "../misc.h"
#include "../position.h"

#include <sstream>
#include <fstream>
#include <cstring>	// std::memset()

using namespace std;

// -----------------------------------
//        局面の圧縮・解凍
// -----------------------------------

// ビットストリームを扱うクラス
// 局面の符号化を行なうときに、これがあると便利
struct BitStream
{
  // データを格納するメモリを事前にセットする。
  // そのメモリは0クリアされているものとする。
  void  set_data(uint8_t* data_) { data = data_; reset(); }

  // set_data()で渡されたポインタの取得。
  uint8_t* get_data() const { return data; }

  // カーソルの取得。
  int get_cursor() const { return bit_cursor; }

  // カーソルのリセット
  void reset() { bit_cursor = 0; }

  // ストリームに1bit書き出す。
  // bは非0なら1を書き出す。0なら0を書き出す。
  void write_one_bit(int b)
  {
    if (b)
      data[bit_cursor / 8] |= 1 << (bit_cursor & 7);

    ++bit_cursor;
  }

  // ストリームから1ビット取り出す。
  int read_one_bit()
  {
    int b = (data[bit_cursor / 8] >> (bit_cursor & 7)) & 1;
    ++bit_cursor;

    return b;
  }

  // nビットのデータを書き出す
  // データはdの下位から順に書き出されるものとする。
  void write_n_bit(int d, int n)
  {
    for (int i = 0; i < n; ++i)
      write_one_bit(d & (1 << i));
  }

  // nビットのデータを読み込む
  // write_n_bit()の逆変換。
  int read_n_bit(int n)
  {
    int result = 0;
    for (int i = 0; i < n; ++i)
      result |= read_one_bit() ? (1 << i) : 0;

    return result;
  }

private:
  // 次に読み書きすべきbit位置。
  int bit_cursor;

  // データの実体
  uint8_t* data;
};


//  ハフマン符号化
//   ※　 なのはminiの符号化から、変換が楽になるように単純化。
//
//   盤上の1升(NO_PIECE以外) = 2～6bit ( + 成りフラグ1bit+ 先後1bit )
//   手駒の1枚               = 1～5bit ( + 成りフラグ1bit+ 先後1bit )
//
//    空     xxxxx0 + 0    (none)
//    歩     xxxx01 + 2    xxxx0 + 2
//    香     xx0011 + 2    xx001 + 2
//    桂     xx1011 + 2    xx101 + 2
//    銀     xx0111 + 2    xx011 + 2
//    金     x01111 + 1    x0111 + 1 // 金は成りフラグはない。
//    角     011111 + 2    01111 + 2
//    飛     111111 + 2    11111 + 2
//
// すべての駒が盤上にあるとして、
//     空 81 - 40駒 = 41升 = 41bit
//     歩      4bit*18駒   = 72bit
//     香      6bit* 4駒   = 24bit
//     桂      6bit* 4駒   = 24bit
//     銀      6bit* 4駒   = 24bit            
//     金      6bit* 4駒   = 24bit
//     角      8bit* 2駒   = 16bit
//     飛      8bit* 2駒   = 16bit
//                          -------
//                          241bit + 1bit(手番) + 7bit×2(王の位置先後) = 256bit
//
// 盤上の駒が手駒に移動すると盤上の駒が空になるので盤上のその升は1bitで表現でき、
// 手駒は、盤上の駒より1bit少なく表現できるので結局、全体のbit数に変化はない。
// ゆえに、この表現において、どんな局面でもこのbit数で表現できる。
// 手駒に成りフラグは不要だが、これも含めておくと盤上の駒のbit数-1になるので
// 全体のbit数が固定化できるのでこれも含めておくことにする。

// Huffman Encoding
//
// Empty  xxxxxxx0
// Pawn   xxxxx001 + 1 bit (Side to move)
// Knight xxxxx011 + 1 bit (Side to move)
// Bishop xxxxx101 + 1 bit (Side to move)
// Rook   xxxxx111 + 1 bit (Side to move)

struct HuffmanedPiece
{
  int code; // どうコード化されるか
  int bits; // 何bit専有するのか
};

HuffmanedPiece huffman_table[] =
{
  {0b000,1}, // NO_PIECE
  {0b001,3}, // PAWN
  {0b011,3}, // KNIGHT
  {0b101,3}, // BISHOP
  {0b111,3}, // ROOK
};

// sfenを圧縮/解凍するためのクラス
// sfenはハフマン符号化をすることで256bit(32bytes)にpackできる。
// このことはなのはminiにより証明された。上のハフマン符号化である。
//
// 内部フォーマット = 手番1bit+王の位置7bit*2 + 盤上の駒(ハフマン符号化) + 手駒(ハフマン符号化)
// Side to move (White = 0, Black = 1) (1bit)
// White King Position (6 bits)
// Black King Position (6 bits)
// Huffman Encoding of the board
// Castling availability (1 bit x 4)
// En passant square (1 or 1 + 6 bits)
// Rule 50 (6 bits)
// Game play (8 bits)
//
// TODO(someone): Rename SFEN to FEN.
//
struct SfenPacker
{
  // sfenをpackしてdata[32]に格納する。
  void pack(const Position& pos)
  {
//    cout << pos;

    memset(data, 0, 32 /* 256bit */);
    stream.set_data(data);

    // 手番
    // Side to move.
    stream.write_one_bit((int)(pos.side_to_move()));

    // 先手玉、後手玉の位置、それぞれ7bit
    // White king and black king, 6 bits for each.
    for(auto c : Colors)
      stream.write_n_bit(pos.king_square(c), 6);

    // Write the pieces on the board other than the kings.
    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
      for (File f = FILE_A; f <= FILE_H; ++f)
      {
        Piece pc = pos.piece_on(make_square(f, r));
        if (type_of(pc) == KING)
          continue;
        write_board_piece_to_stream(pc);
      }
    }

    // TODO(someone): Support chess960.
    stream.write_one_bit(pos.can_castle(WHITE_OO));
    stream.write_one_bit(pos.can_castle(WHITE_OOO));
    stream.write_one_bit(pos.can_castle(BLACK_OO));
    stream.write_one_bit(pos.can_castle(BLACK_OOO));

    if (pos.ep_square() == SQ_NONE) {
      stream.write_one_bit(0);
    }
    else {
      stream.write_one_bit(1);
      stream.write_n_bit(static_cast<int>(pos.ep_square()), 6);
    }

    stream.write_n_bit(pos.state()->rule50, 6);

    stream.write_n_bit(pos.game_ply(), 8);

    assert(stream.get_cursor() <= 256);
  }

  // pack()でpackされたsfen(256bit = 32bytes)
  // もしくはunpack()でdecodeするsfen
  uint8_t *data; // uint8_t[32];

//private:
  // Position::set_from_packed_sfen(uint8_t data[32])でこれらの関数を使いたいので筋は悪いがpublicにしておく。

  BitStream stream;

  // 盤面の駒をstreamに出力する。
  void write_board_piece_to_stream(Piece pc)
  {
    // 駒種
    PieceType pr = type_of(pc);
    auto c = huffman_table[pr];
    stream.write_n_bit(c.code, c.bits);
 
    if (pc == NO_PIECE)
      return;

    // 先後フラグ
    stream.write_one_bit(color_of(pc));
  }

  // 盤面の駒を1枚streamから読み込む
  Piece read_board_piece_from_stream()
  {
    PieceType pr = NO_PIECE_TYPE;
    int code = 0, bits = 0;
    while (true)
    {
      code |= stream.read_one_bit() << bits;
      ++bits;

      assert(bits <= 6);

      for (pr = NO_PIECE_TYPE; pr < KING; ++pr)
        if (huffman_table[pr].code == code
          && huffman_table[pr].bits == bits)
          goto Found;
    }
  Found:;
    if (pr == NO_PIECE_TYPE)
      return NO_PIECE;

    // 先後フラグ
    Color c = (Color)stream.read_one_bit();
    
    return make_piece(c, pr);
  }
};


// -----------------------------------
//        Positionクラスに追加
// -----------------------------------

// 高速化のために直接unpackする関数を追加。かなりしんどい。
// packer::unpack()とPosition::set()とを合体させて書く。
// 渡された局面に問題があって、エラーのときは非0を返す。
int Position::set_from_packed_sfen(const PackedSfen& sfen , StateInfo * si, Thread* th, bool mirror)
{
	SfenPacker packer;
	auto& stream = packer.stream;
	stream.set_data((uint8_t*)&sfen);

	std::memset(this, 0, sizeof(Position));
	std::memset(si, 0, sizeof(StateInfo));
	st = si;

	// Active color
	sideToMove = (Color)stream.read_one_bit();

	// evalListのclear。上でmemsetでゼロクリアしたときにクリアされているが…。
	evalList.clear();

	// PieceListを更新する上で、どの駒がどこにあるかを設定しなければならないが、
	// それぞれの駒をどこまで使ったかのカウンター
	PieceNumber piece_no_count[KING] = {
    PIECE_NUMBER_ZERO,
    PIECE_NUMBER_PAWN,
    PIECE_NUMBER_KNIGHT,
		PIECE_NUMBER_BISHOP,
    PIECE_NUMBER_ROOK,
  };

  pieceList[W_KING][0] = SQUARE_NB;
  pieceList[B_KING][0] = SQUARE_NB;

	// まず玉の位置
	if (mirror)
	{
		for (auto c : Colors)
			board[Mir((Square)stream.read_n_bit(7))] = make_piece(c, KING);
	}
	else
	{
		for (auto c : Colors)
			board[stream.read_n_bit(7)] = make_piece(c, KING);
	}

  // Piece placement
  for (Rank r = RANK_8; r >= RANK_1; --r)
  {
    for (File f = FILE_A; f <= FILE_H; ++f)
    {
      auto sq = make_square(f, r);
      if (mirror) {
        sq = Mir(sq);
      }

      // すでに玉がいるようだ
      Piece pc;
      if (type_of(board[sq]) != KING)
      {
        assert(board[sq] == NO_PIECE);
        pc = packer.read_board_piece_from_stream();
      }
      else
      {
        pc = board[sq];
        board[sq] = NO_PIECE; // いっかい取り除いておかないとput_piece()でASSERTに引っかかる。
      }

      // 駒がない場合もあるのでその場合はスキップする。
      if (pc == NO_PIECE)
        continue;

      put_piece(Piece(pc), sq);

      // evalListの更新
      PieceNumber piece_no =
        (pc == B_KING) ? PIECE_NUMBER_BKING : // 先手玉
        (pc == W_KING) ? PIECE_NUMBER_WKING : // 後手玉
        piece_no_count[type_of(pc)]++; // それ以外

      evalList.put_piece(piece_no, sq, pc); // sqの升にpcの駒を配置する

      //cout << sq << ' ' << board[sq] << ' ' << stream.get_cursor() << endl;

      if (stream.get_cursor() > 256)
        return 1;
      //assert(stream.get_cursor() <= 256);

    }
  }

  // Castling availability.
  // TODO(someone): Support chess960.
  st->castlingRights = 0;
  if (stream.read_one_bit()) {
    Square rsq;
    for (rsq = relative_square(WHITE, SQ_H1); piece_on(rsq) != W_ROOK; --rsq) {}
    set_castling_right(WHITE, rsq);
  }
  if (stream.read_one_bit()) {
    Square rsq;
    for (rsq = relative_square(WHITE, SQ_A1); piece_on(rsq) != W_ROOK; ++rsq) {}
    set_castling_right(WHITE, rsq);
  }
  if (stream.read_one_bit()) {
    Square rsq;
    for (rsq = relative_square(BLACK, SQ_H1); piece_on(rsq) != W_ROOK; --rsq) {}
    set_castling_right(BLACK, rsq);
  }
  if (stream.read_one_bit()) {
    Square rsq;
    for (rsq = relative_square(BLACK, SQ_A1); piece_on(rsq) != W_ROOK; ++rsq) {}
    set_castling_right(BLACK, rsq);
  }

  // En passant square. Ignore if no pawn capture is possible
  if (stream.read_one_bit()) {
    Square ep_square = static_cast<Square>(stream.read_n_bit(6));
    st->epSquare = ep_square;

    if (!(attackers_to(st->epSquare) & pieces(sideToMove, PAWN))
      || !(pieces(~sideToMove, PAWN) & (st->epSquare + pawn_push(~sideToMove))))
      st->epSquare = SQ_NONE;
  }

  // Halfmove clock
  st->rule50 = static_cast<Square>(stream.read_n_bit(6));

  // Fullmove number
  gamePly = static_cast<Square>(stream.read_n_bit(8));
  // Convert from fullmove starting from 1 to gamePly starting from 0,
  // handle also common incorrect FEN with fullmove = 0.
  gamePly = std::max(2 * (gamePly - 1), 0) + (sideToMove == BLACK);

  assert(stream.get_cursor() <= 256);

  chess960 = false;
  thisThread = th;
	set_state(st);

  assert(pos_is_ok());
#if defined(EVAL_NNUE)
  assert(evalList.is_valid(*this));
#endif  // defined(EVAL_NNUE)

	return 0;
}

// 盤面と手駒、手番を与えて、そのsfenを返す。
//std::string Position::sfen_from_rawdata(Piece board[81], Hand hands[2], Color turn, int gamePly_)
//{
//  // 内部的な構造体にコピーして、sfen()を呼べば、変換過程がそこにしか依存していないならば
//  // これで正常に変換されるのでは…。
//  Position pos;
//
//  memcpy(pos.board, board, sizeof(Piece) * 81);
//  memcpy(pos.hand, hands, sizeof(Hand) * 2);
//  pos.sideToMove = turn;
//  pos.gamePly = gamePly_;
//
//  return pos.sfen();
//
//  // ↑の実装、美しいが、いかんせん遅い。
//  // 棋譜を大量に読み込ませて学習させるときにここがボトルネックになるので直接unpackする関数を書く。
//}

// packされたsfenを得る。引数に指定したバッファに返す。
void Position::sfen_pack(PackedSfen& sfen)
{
  SfenPacker sp;
  sp.data = (uint8_t*)&sfen;
  sp.pack(*this);
}

//// packされたsfenを解凍する。sfen文字列が返る。
//std::string Position::sfen_unpack(const PackedSfen& sfen)
//{
//  SfenPacker sp;
//  sp.data = (uint8_t*)&sfen;
//  return sp.unpack();
//}


#endif // USE_SFEN_PACKER

