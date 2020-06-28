#include "learning_tools.h"

#if defined (EVAL_LEARN)

#if defined(_OPENMP)
#include <omp.h>
#endif
#include "../misc.h"

using namespace Eval;

namespace EvalLearningTools
{

	// --- static variables

	double Weight::eta;
	double Weight::eta1;
	double Weight::eta2;
	double Weight::eta3;
	uint64_t Weight::eta1_epoch;
	uint64_t Weight::eta2_epoch;

	std::vector<bool> min_index_flag;

	// --- initialization for each individual table

	void init_min_index_flag()
	{
		// Initialization of mir_piece and inv_piece must be completed.
		assert(mir_piece(Eval::f_pawn) == Eval::e_pawn);

		// Initialize the flag array for dimension reduction
		// Not involved in KPPP.

		KK g_kk;
		g_kk.set(SQUARE_NB, Eval::fe_end, 0);
		KKP g_kkp;
		g_kkp.set(SQUARE_NB, Eval::fe_end, g_kk.max_index());
		KPP g_kpp;
		g_kpp.set(SQUARE_NB, Eval::fe_end, g_kkp.max_index());

		uint64_t size = g_kpp.max_index();
		min_index_flag.resize(size);

#pragma omp parallel
		{
#if defined(_OPENMP)
			// To prevent the logical 64 cores from being used when there are two CPUs under Windows
			// explicitly assign to CPU here
			int thread_index = omp_get_thread_num(); // get your thread number
			WinProcGroup::bindThisThread(thread_index);
#endif

#pragma omp for schedule(dynamic,20000)

			for (int64_t index_ = 0; index_ < (int64_t)size; ++index_)
			{
				// It seems that the loop variable must be a sign type due to OpenMP restrictions, but
				// It's really difficult to use.
				uint64_t index = (uint64_t)index_;

				if (g_kk.is_ok(index))
				{
					// Make sure that the original index will be restored by conversion from index and reverse conversion.
					// It is a process that is executed only once at startup, so write it in assert.
					assert(g_kk.fromIndex(index).toIndex() == index);

					KK a[KK_LOWER_COUNT];
					g_kk.fromIndex(index).toLowerDimensions(a);

					// Make sure that the first element of dimension reduction is the same as the original index.
					assert(a[0].toIndex() == index);

					uint64_t min_index = UINT64_MAX;
					for (auto& e : a)
						min_index = std::min(min_index, e.toIndex());
					min_index_flag[index] = (min_index == index);
				}
				else if (g_kkp.is_ok(index))
				{
					assert(g_kkp.fromIndex(index).toIndex() == index);

					KKP x = g_kkp.fromIndex(index);
					KKP a[KKP_LOWER_COUNT];
					x.toLowerDimensions(a);

					assert(a[0].toIndex() == index);

					uint64_t min_index = UINT64_MAX;
					for (auto& e : a)
						min_index = std::min(min_index, e.toIndex());
					min_index_flag[index] = (min_index == index);
				}
				else if (g_kpp.is_ok(index))
				{
					assert(g_kpp.fromIndex(index).toIndex() == index);

					KPP x = g_kpp.fromIndex(index);
					KPP a[KPP_LOWER_COUNT];
					x.toLowerDimensions(a);

					assert(a[0].toIndex() == index);

					uint64_t min_index = UINT64_MAX;
					for (auto& e : a)
						min_index = std::min(min_index, e.toIndex());
					min_index_flag[index] = (min_index == index);
				}
				else
				{
					assert(false);
				}
			}
		}
	}

	void learning_tools_unit_test_kpp()
	{

		// test KPP triangulation for bugs
		// All combinations of k-p0-p1 are properly handled by KPP, and the dimension reduction at that time is
		// Determine if it is correct.

		KK g_kk;
		g_kk.set(SQUARE_NB, Eval::fe_end, 0);
		KKP g_kkp;
		g_kkp.set(SQUARE_NB, Eval::fe_end, g_kk.max_index());
		KPP g_kpp;
		g_kpp.set(SQUARE_NB, Eval::fe_end, g_kkp.max_index());

		std::vector<bool> f;
		f.resize(g_kpp.max_index() - g_kpp.min_index());

		for(auto k = SQUARE_ZERO ; k < SQUARE_NB ; ++k)
			for(auto p0 = BonaPiece::BONA_PIECE_ZERO; p0 < fe_end ; ++p0)
				for (auto p1 = BonaPiece::BONA_PIECE_ZERO; p1 < fe_end; ++p1)
				{
					KPP kpp_org = g_kpp.fromKPP(k,p0,p1);
					KPP kpp0;
					KPP kpp1 = g_kpp.fromKPP(Mir(k), mir_piece(p0), mir_piece(p1));
					KPP kpp_array[2];

					auto index = kpp_org.toIndex();
					assert(g_kpp.is_ok(index));

					kpp0 = g_kpp.fromIndex(index);

					//if (kpp0 != kpp_org)
					//	std::cout << "index = " << index << "," << kpp_org << "," << kpp0 << std::endl;

					kpp0.toLowerDimensions(kpp_array);

					assert(kpp_array[0] == kpp0);
					assert(kpp0 == kpp_org);
					assert(kpp_array[1] == kpp1);

					auto index2 = kpp1.toIndex();
					f[index - g_kpp.min_index()] = f[index2-g_kpp.min_index()] = true;
				}

		// Check if there is no missing index.
		for(size_t index = 0 ; index < f.size(); index++)
			if (!f[index])
			{
				std::cout << index << g_kpp.fromIndex(index + g_kpp.min_index()) << std::endl;
			}
	}

	void learning_tools_unit_test_kppp()
	{
		// Test for missing KPPP calculations

		KPPP g_kppp;
		g_kppp.set(15, Eval::fe_end,0);
		uint64_t min_index = g_kppp.min_index();
		uint64_t max_index = g_kppp.max_index();

		// Confirm last element.
		//KPPP x = KPPP::fromIndex(max_index-1);
		//std::cout << x << std::endl;

		for (uint64_t index = min_index; index < max_index; ++index)
		{
			KPPP x = g_kppp.fromIndex(index);
			//std::cout << x << std::endl;

#if 0
			if ((index % 10000000) == 0)
				std::cout << "index = " << index << std::endl;

			// index = 9360000000
			//	done.

			if (x.toIndex() != index)
			{
				std::cout << "assertion failed , index = " << index << std::endl;
			}
#endif

			assert(x.toIndex() == index);

//			ASSERT((&kppp_ksq_pcpcpc(x.king(), x.piece0(), x.piece1(), x.piece2()) - &kppp[0][0]) == (index - min_index));
		}

	}

	void learning_tools_unit_test_kkpp()
	{
		KKPP g_kkpp;
		g_kkpp.set(SQUARE_NB, 10000, 0);
		uint64_t n = 0;
		for (int k = 0; k<SQUARE_NB; ++k)
			for (int i = 0; i<10000; ++i) // As a test, assuming a large fe_end, try turning at 10000.
				for (int j = 0; j < i; ++j)
				{
					auto kkpp = g_kkpp.fromKKPP(k, (BonaPiece)i, (BonaPiece)j);
					auto r = kkpp.toRawIndex();
					assert(n++ == r);
					auto kkpp2 = g_kkpp.fromIndex(r + g_kkpp.min_index());
					assert(kkpp2.king() == k && kkpp2.piece0() == i && kkpp2.piece1() == j);
				}
	}

	// Initialize this entire EvalLearningTools
	void init()
	{
		// Initialization is required only once after startup, so a flag for that.
		static bool first = true;

		if (first)
		{
			std::cout << "EvalLearningTools init..";

			// Make mir_piece() and inv_piece() available.
			// After this, the min_index_flag is initialized, but
			// It depends on this, so you need to do this first.
			init_mir_inv_tables();

			//learning_tools_unit_test_kpp();
			//learning_tools_unit_test_kppp();
			//learning_tools_unit_test_kkpp();

			// It may be the last time to execute UnitTest, but since init_min_index_flag() takes a long time,
			// I want to do this at the time of debugging.

			init_min_index_flag();

			std::cout << "done." << std::endl;

			first = false;
		}
	}
}

#endif
