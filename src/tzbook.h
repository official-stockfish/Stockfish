#ifndef TZBOOK_H_INCLUDED
#define TZBOOK_H_INCLUDED

#include "bitboard.h"
#include "position.h"
#include "string.h"

struct TZHash2
{
    uint32_t key1;
    uint16_t key2;
    unsigned char move_number;
    unsigned char move_number2;
};

class TZBook
{
  public:

   Bitboard last_position;
   Bitboard akt_position;
   int last_anz_pieces;
   int akt_anz_pieces;
   int search_counter;

   bool enabled, do_search;
   int  book_move2_probability;
 
   TZBook();
   ~TZBook();

    void init(const std::string& path);
    void set_book_move2_probability(int book_move2_prob);

    Move probe2(Position& pos);
    TZHash2 *probe2(Key key);

private:

    int keycount;
    TZHash2 *tzhash2;
    bool check_draw(Move m, Position& pos);
    Move get_move_from_draw_position(Position& pos, TZHash2 *tz);
    Move get_move(Position& pos, TZHash2 *tz);
    Move movenumber_to_move(Position& pos, int n);
};

extern TZBook tzbook;

#endif // #ifndef TZBOOK_H_INCLUDED