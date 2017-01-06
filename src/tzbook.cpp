#include "tzbook.h"
#include "uci.h"
#include "movegen.h"
#include "thread.h"
#include <iostream>
#include "misc.h"
#include <sys/timeb.h>

TZBook tzbook;  // global TZBook

using namespace std;

int qsort_compare_int(const void* a, const void* b)
{
    const int int_a = *((const int*)a);
    const int int_b = *((const int*)b);

    if (int_a == int_b) return 0;
    else if (int_a < int_b) return -1;
    else return 1;
}

TZBook::TZBook()
{
    keycount = 0;  
    book_move2_probability = 0;
    last_position = 0;
    akt_position = 0;
    last_anz_pieces = 0;
    akt_anz_pieces = 0;
    search_counter = 0;
    tzhash2 = NULL;
    do_search = true;  
    enabled = false;
}

TZBook::~TZBook()
{
}


void TZBook::init(const std::string& path)
{
    if (path.length() == 0) return;

    const char *p = path.c_str();
    if (strcmp(p, "<empty>") == 0)
        return;

    FILE *fpt = fopen(p, "rb");
    if (fpt == NULL)
    {
        sync_cout << "info string Could not open " << path << sync_endl;
        return;
    }

    if (tzhash2 != NULL)
    {
        free(tzhash2);
        tzhash2 = NULL;
    }

    fseek(fpt, 0L, SEEK_END);
    int filesize = ftell(fpt);
    fseek(fpt, 0L, SEEK_SET);

    keycount = filesize / 8;
    tzhash2 = new TZHash2[keycount];
    
    fread(tzhash2, 1, filesize, fpt);
    fclose(fpt);

    sync_cout << "info string Book loaded: " << path << sync_endl;

    srand((int)time(NULL));
    enabled = true;
}


void TZBook::set_book_move2_probability(int book_move2_prob)
{
    book_move2_probability = book_move2_prob;
}


Move TZBook::probe2(Position& pos)
{
    Move m = MOVE_NONE;
    if (!enabled) return m;

    akt_position = pos.pieces();
    akt_anz_pieces = popcount(akt_position);

    if (do_search == false)
    {
        Bitboard b = akt_position ^ last_position;
        int n2 = popcount(b);

        if (n2 > 4) do_search = true;
        if (akt_anz_pieces > last_anz_pieces) do_search = true;
        if (akt_anz_pieces < last_anz_pieces - 2) do_search = true;
    }

    last_position = akt_position;
    last_anz_pieces = akt_anz_pieces;

    if (do_search)
    {
        TZHash2 *tz = probe2(pos.key());
        if (tz == NULL)
        {
            search_counter++;
            if (search_counter > 2)
            {
                do_search = false;
                search_counter = 0;
            }
        }
        else
        {
            if (pos.is_draw(64))
                m = get_move_from_draw_position(pos, tz);
            else
                m = get_move(pos, tz);
        }
    }

    return m;
}


TZHash2 *TZBook::probe2(Key key)
{
    uint32_t key1 = key >> 32;
    unsigned short key2 = key >> 16 & 0xFFFF;

    int start = 0;
    int end = keycount;

    for (;;)
    {
        int mid = (end + start) / 2;

        if (tzhash2[mid].key1 < key1)
            start = mid;
        else
        {
            if (tzhash2[mid].key1 > key1)
                end = mid;
            else
            {
                start = max(mid - 4, 0);
                end = min(mid + 4, keycount);
            }
        }

        if (end - start < 9)
            break;
    }

    for (int i = start; i < end; i++)
        if ((key1 == tzhash2[i].key1) && (key2 == tzhash2[i].key2))
            return &(tzhash2[i]);

    return NULL;
}

Move TZBook::movenumber_to_move(Position& pos, int n)
{
    const ExtMove *m = MoveList<LEGAL>(pos).begin();
    size_t size = MoveList<LEGAL>(pos).size();
    Move *mv = new Move[size];
    for (unsigned int i = 0; i < size; i++)
        mv[i] = m[i].move;

    qsort(mv, size, sizeof(mv[0]), qsort_compare_int);

    return mv[n];
}

bool TZBook::check_draw(Move m, Position& pos)
{
    StateInfo st;

    pos.do_move(m, st, pos.gives_check(m));   
    bool draw = pos.is_draw(64);
    pos.undo_move(m);

    return draw;
}


Move TZBook::get_move_from_draw_position(Position& pos, TZHash2 *tz)
{
    Move m = movenumber_to_move(pos, tz->move_number);
    if (!check_draw(m, pos))
        return m;

    if (tz->move_number2 == 255)
        return m;

    m = movenumber_to_move(pos, tz->move_number2);
    if (!check_draw(m, pos))
        return m;
       
    return MOVE_NONE;
}


Move  TZBook::get_move(Position& pos, TZHash2 *tz)
{
    Move m1 = movenumber_to_move(pos, tz->move_number);
    if ((book_move2_probability == 0) || (tz->move_number2 == 255))
        return m1;

    Move m2 = movenumber_to_move(pos, tz->move_number2);
    if ((book_move2_probability == 100) || (rand() % 100 < book_move2_probability))
        return m2;

    return m1;
}