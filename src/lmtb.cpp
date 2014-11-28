#include "lmtb.h"

HMODULE hLib;

const char * proc_names[] = {
	"dll_add_table_path",
	"dll_set_table_path",
	"dll_set_cache_size",
	"dll_clear_cache",
	"dll_clear_cache_all",
	"dll_set_table_order",
	"dll_get_table_order",
	"dll_get_max_pieces_count",
	"dll_get_max_pieces_count_with_order",
	"dll_get_table_name",
	"dll_get_missing_table_name",
	"dll_probe_fen",
	"dll_probe_fen_with_order",
	"dll_probe_fen_dtmz50",
	"dll_probe_position",
	"dll_probe_position_with_order",
	"dll_probe_position_dtmz50"
#ifndef TB_DLL_EXPORT
	,"dll_get_number_load_from_cache"
	,"dll_get_number_load_from_file"
	,"dll_get_number_pop_from_cache"
	,"dll_get_number_in_cache"
	,"dll_get_cache_size"
	,"dll_get_hidden_size"
	,"dll_set_logging"
	,"dll_set_hidden_cache_clean_percent"
	,"dll_print_statistics"
	,"dll_probe_fen_special_mate_state"
	,"dll_get_tree_fen"
	,"dll_get_tree_bounded_fen"
	,"dll_get_best_move_fen"
	,"dll_get_line_fen"
	,"dll_get_line_bounded_fen"
#endif
};

add_table_path tb_add_table_path;
add_table_path tb_set_table_path;
set_cache_size tb_set_cache_size;
clear_cache tb_clear_cache;
clear_cache_all tb_clear_cache_all;
set_table_order tb_set_table_order;
get_table_order tb_get_table_order;
get_max_pieces_count tb_get_max_pieces_count;
get_max_pieces_count_with_order tb_get_max_pieces_count_with_order;
get_table_name tb_get_table_name;
get_missing_table_name tb_get_missing_table_name;
probe_fen tb_probe_fen;
probe_fen_with_order tb_probe_fen_with_order;
probe_fen_with_order tb_probe_fen_dtmz50;
probe_position tb_probe_position;
probe_position_with_order tb_probe_position_with_order;
probe_position_with_order tb_probe_position_dtmz50;
#ifndef TB_DLL_EXPORT
get_cache_size tb_get_number_load_from_cache;
get_cache_size tb_get_number_load_from_file;
get_cache_size tb_get_number_pop_from_cache;
get_cache_size tb_get_number_in_cache;
get_cache_size tb_get_cache_size;
get_cache_size tb_get_hidden_size;
clear_cache tb_set_logging;
set_cache_size tb_set_hidden_cache_clean_percent;
add_table_path tb_print_statistics;
probe_fen tb_probe_fen_special_mate_state;
get_tree_fen tb_get_tree_fen;
get_tree_bounded_fen tb_get_tree_bounded_fen;
get_tree_fen tb_get_best_move_fen;
get_tree_fen tb_get_line_fen;
get_line_bounded_fen tb_get_line_bounded_fen;
#endif

int load_lomonosov_tb(void)
{
	hLib = LoadLibrary(TEXT("lomonosov_tb.dll"));
	if (!hLib) return -1;
	(FARPROC &)tb_add_table_path = GetProcAddress(hLib, proc_names[FUNC_ADD_TABLE_PATH]);
	if (!tb_add_table_path) return -1;
	(FARPROC &)tb_set_table_path = GetProcAddress(hLib, proc_names[FUNC_SET_TABLE_PATH]);
	if (!tb_set_table_path) return -1;
	(FARPROC &)tb_set_cache_size = GetProcAddress(hLib, proc_names[FUNC_SET_CACHE_SIZE]);
	if (!tb_set_cache_size) return -1;
	(FARPROC &)tb_clear_cache = GetProcAddress(hLib, proc_names[FUNC_CLEAR_CACHE]);
	if (!tb_clear_cache) return -1;
	(FARPROC &)tb_clear_cache_all = GetProcAddress(hLib, proc_names[FUNC_CLEAR_CACHE_ALL]);
	if (!tb_clear_cache_all) return -1;
	(FARPROC &)tb_set_table_order = GetProcAddress(hLib, proc_names[FUNC_SET_TABLE_ORDER]);
	if (!tb_set_table_order) return -1;
	(FARPROC &)tb_get_table_order = GetProcAddress(hLib, proc_names[FUNC_GET_TABLE_ORDER]);
	if (!tb_get_table_order) return -1;
	(FARPROC &)tb_get_max_pieces_count = GetProcAddress(hLib, proc_names[FUNC_GET_MAX_PIECES_COUNT]);
	if (!tb_get_max_pieces_count) return -1;
	(FARPROC &)tb_get_max_pieces_count_with_order = GetProcAddress(hLib, proc_names[FUNC_GET_MAX_PIECES_COUNT_ORDER]);
	if (!tb_get_max_pieces_count_with_order) return -1;
	(FARPROC &)tb_get_table_name = GetProcAddress(hLib, proc_names[FUNC_GET_TABLE_NAME]);
	if (!tb_get_table_name) return -1;
	(FARPROC &)tb_get_missing_table_name = GetProcAddress(hLib, proc_names[FUNC_GET_MISSING_TABLE_NAME]);
	if (!tb_get_missing_table_name) return -1;
	(FARPROC &)tb_probe_fen = GetProcAddress(hLib, proc_names[FUNC_PROBE_FEN]);
	if (!tb_probe_fen) return -1;
	(FARPROC &)tb_probe_fen_with_order = GetProcAddress(hLib, proc_names[FUNC_PROBE_FEN_WITH_ORDER]);
	if (!tb_probe_fen_with_order) return -1;
	(FARPROC &)tb_probe_fen_dtmz50 = GetProcAddress(hLib, proc_names[FUNC_PROBE_FEN_DTMZ50]);
	if (!tb_probe_fen_dtmz50) return -1;
	(FARPROC &)tb_probe_position = GetProcAddress(hLib, proc_names[FUNC_PROBE_POSITION]);
	if (!tb_probe_position) return -1;
	(FARPROC &)tb_probe_position_with_order = GetProcAddress(hLib, proc_names[FUNC_PROBE_POSITION_WITH_ORDER]);
	if (!tb_probe_position_with_order) return -1;
	(FARPROC &)tb_probe_position_dtmz50 = GetProcAddress(hLib, proc_names[FUNC_PROBE_POSITION_DTMZ50]);
	if (!tb_probe_position_dtmz50) return -1;
#ifndef TB_DLL_EXPORT
	(FARPROC &)tb_get_number_load_from_cache = GetProcAddress(hLib, proc_names[FUNC_GET_NUMBER_LOAD_FROM_CACHE]);
	if (!tb_get_number_load_from_cache) return -1;
	(FARPROC &)tb_get_number_load_from_file = GetProcAddress(hLib, proc_names[FUNC_GET_NUMBER_LOAD_FROM_FILE]);
	if (!tb_get_number_load_from_file) return -1;
	(FARPROC &)tb_get_number_pop_from_cache = GetProcAddress(hLib, proc_names[FUNC_GET_NUMBER_POP_FROM_CACHE]);
	if (!tb_get_number_pop_from_cache) return -1;
	(FARPROC &)tb_get_number_in_cache = GetProcAddress(hLib, proc_names[FUNC_GET_NUMBER_IN_CACHE]);
	if (!tb_get_number_in_cache) return -1;
	(FARPROC &)tb_get_cache_size = GetProcAddress(hLib, proc_names[FUNC_GET_CACHE_SIZE]);
	if (!tb_get_cache_size) return -1;
	(FARPROC &)tb_get_hidden_size = GetProcAddress(hLib, proc_names[FUNC_GET_HIDDEN_SIZE]);
	if (!tb_get_hidden_size) return -1;
	(FARPROC &)tb_set_logging = GetProcAddress(hLib, proc_names[FUNC_SET_LOGGING]);
	if (!tb_set_logging) return -1;
	(FARPROC &)tb_set_hidden_cache_clean_percent = GetProcAddress(hLib, proc_names[FUNC_SET_HIDDEN_CACHE_CLEAN_PERCENT]);
	if (!tb_set_hidden_cache_clean_percent) return -1;
	(FARPROC &)tb_print_statistics = GetProcAddress(hLib, proc_names[FUNC_PRINT_STATISTICS]);
	if (!tb_print_statistics) return -1;
	(FARPROC &)tb_probe_fen_special_mate_state = GetProcAddress(hLib, proc_names[FUNC_PROBE_FEN_SPECIAL_MATE_STATE]);
	if (!tb_probe_fen_special_mate_state) return -1;
	(FARPROC &)tb_get_tree_fen = GetProcAddress(hLib, proc_names[FUNC_GET_TREE_FEN]);
	if (!tb_get_tree_fen) return -1;
	(FARPROC &)tb_get_tree_bounded_fen = GetProcAddress(hLib, proc_names[FUNC_GET_TREE_BOUNDED_FEN]);
	if (!tb_get_tree_bounded_fen) return -1;
	(FARPROC &)tb_get_best_move_fen = GetProcAddress(hLib, proc_names[FUNC_GET_BEST_MOVE_FEN]);
	if (!tb_get_best_move_fen) return -1;
	(FARPROC &)tb_get_line_fen = GetProcAddress(hLib, proc_names[FUNC_GET_LINE_FEN]);
	if (!tb_get_line_fen) return -1;
	(FARPROC &)tb_get_line_bounded_fen = GetProcAddress(hLib, proc_names[FUNC_GET_LINE_BOUNDED_FEN]);
	if (!tb_get_line_bounded_fen) return -1;
#endif
	return 0;
}

void unload_lomonosov_tb()
{
	if (hLib) FreeLibrary(hLib);
}