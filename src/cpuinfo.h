/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CPUINFO_H_INCLUDED
#define CPUINFO_H_INCLUDED

#include <stdint.h>
#include <cstring>
#include <string>
#include <bitset>
#include <vector>
#include <array>

namespace Stockfish {

    class CpuInfo
    {
        class CpuId;

    public:
        static std::string vendor() { return CPUID._vendor; }
        static std::string brand()  { return CPUID._brand; }
        static std::string infoString();

        static bool isIntel()    { return CPUID._isIntel; }
        static bool isAMD()      { return CPUID._isAMD; }
        static bool isAMDZen3()  { return CPUID._isAMD && CPUID._family > 24; }
        static bool osAVX();
        static bool osAVX2();
        static bool osAVX512();

        // flags reported by function 0x01
        static bool SSE3()       { return CPUID._f1_ECX[0]; }  // -msse3
        static bool SSSE3()      { return CPUID._f1_ECX[9]; }  // -DUSE_SSSE3 -mssse3
        static bool SSE41()      { return CPUID._f1_ECX[19]; } // -DUSE_SSE41 -msse4.1
        static bool POPCNT()     { return CPUID._f1_ECX[23]; } // -DUSE_POPCNT -mpopcnt
        static bool OSXSAVE()    { return CPUID._f1_ECX[27]; } // OS uses XSAVE/XRSTOR
        static bool AVX()        { return CPUID._f1_ECX[28]; } // AVX supported by CPU
        static bool MMX()        { return CPUID._f1_EDX[23]; } // -DUSE_MMX -mmmx
        static bool SSE()        { return CPUID._f1_EDX[25]; } // -msse
        static bool SSE2()       { return CPUID._f1_EDX[26]; } // -DUSE_SSE2 -msse2
        // flags reported by function 0x07
        static bool AVX2()       { return CPUID._f7_EBX[5]; }  // -mavx2
        static bool BMI2()       { return CPUID._f7_EBX[8]; }  // -DUSE_PEXT -mbmi2
        static bool AVX512F()    { return CPUID._f7_EBX[16]; } // -mavx512f
        static bool AVX512DQ()   { return CPUID._f7_EBX[17]; } // -mavx512dq
        static bool AVX512BW()   { return CPUID._f7_EBX[30]; } // -mavx512bw
        static bool AVX512VL()   { return CPUID._f7_EBX[31]; } // -mavx512vl
        static bool AVX512VNNI() { return CPUID._f7_ECX[11]; } // -mavx512vnni
        // flags reported by function 0x0D

        // flags reported by extended function 0x80000001
        static bool X64()        { return CPUID._f81_EDX[29]; } // -DIS_64BIT

    private:
        static const CpuId CPUID;

        class CpuId
        {
        public:
            CpuId() :
                _idMax{ 0 },
                _idExtMax{ 0 },
                _isIntel{ false },
                _isAMD{ false },
                _f1_EAX{ 0 },
                _f1_ECX{ 0 },
                _f1_EDX{ 0 },
                _f7_EBX{ 0 },
                _f7_ECX{ 0 },
                _f7_EDX{ 0 },
                _fD_EAX{ 0 },
                _fD_EDX{ 0 },
                _f81_EDX{ 0 },
                _data{},
                _dataExt{},
                _family{ 0 },
                _model{ 0 },
                _stepping{ 0 }
            {
                std::array<int32_t, 4> info;

                // calling cpuid with 0x0
                // gets the number of the highest valid function ID
                cpuid(info.data(), 0, 0);
                _idMax = info[0];
                // call each function and store results in _data
                for (uint32_t i = 0; i <= _idMax; ++i)
                {
                    cpuid(info.data(), i, 0);
                    _data.push_back(info);
                }

                // retrieve CPU vendor string
                char vendor[3*sizeof(int32_t) + 1] { 0 };
                memcpy(vendor,     &_data[0][1], sizeof(int32_t));
                memcpy(vendor + 4, &_data[0][3], sizeof(int32_t));
                memcpy(vendor + 8, &_data[0][2], sizeof(int32_t));
                _vendor = vendor;

                if (_vendor == "GenuineIntel")
                {
                    _isIntel = true;
                }
                else if (_vendor == "AuthenticAMD")
                {
                    _isAMD = true;
                }

                // load bitsets with flags for function 0x01
                if (_idMax >= 0x01)
                {
                    _f1_EAX = _data[1][0];
                    _f1_ECX = _data[1][2];
                    _f1_EDX = _data[1][3];
                }

                // load bitsets with flags for function 0x07
                if (_idMax >= 0x07)
                {
                    _f7_EBX = _data[7][1];
                    _f7_ECX = _data[7][2];
                    _f7_EDX = _data[7][3];
                }

                // load output of function 0x0D
                if (_idMax >= 0x0D)
                {
                    _fD_EAX = _data[13][0];
                    _fD_EDX = _data[13][3];
                }

                // calling cpuid with 0x80000000
                // gets the number of the highest valid extended function ID
                cpuid(info.data(), 0x80000000, 0);
                _idExtMax = info[0];
                // call each extended function and store results in _dataExt
                for (uint32_t i = 0x80000000; i <= _idExtMax; ++i)
                {
                    cpuid(info.data(), i, 0);
                    _dataExt.push_back(info);
                }

                // load bitset with flags for extended function 0x80000001
                if (_idExtMax >= 0x80000001)
                {
                    _f81_EDX = _dataExt[1][3];
                }

                // retrieve CPU brand string if reported
                if (_idExtMax >= 0x80000004)
                {
                    char brand[3*sizeof(info) + 1] { 0 };
                    memcpy(brand,      _dataExt[2].data(), sizeof(info));
                    memcpy(brand + 16, _dataExt[3].data(), sizeof(info));
                    memcpy(brand + 32, _dataExt[4].data(), sizeof(info));
                    _brand = brand;
                }
                
                // compute X86 Family and Model
                _family = (_f1_EAX >> 8) & 0x0F;
                _model = (_f1_EAX >> 4) & 0x0F;
                _stepping = _f1_EAX & 0x0F;
                int32_t ext_family = 0;
                int32_t ext_model = 0;
                // The "Intel 64 and IA-32 Architectures Developer's Manual: Vol. 2A" specifies the Extended Model
                // is defined only when the Base Family is 06h or 0Fh.
                // The "AMD CPUID Specification" specifies that the Extended Model is defined only when Base Family is 0Fh.
                // Both manuals define the display model as {ExtendedModel[3:0],BaseModel[3:0]} in that case.
                if (_family == 0x0F || (_family == 0x06 && _isIntel))
                {
                    ext_model = (_f1_EAX >> 16) & 0x0F;
                    _model += ext_model << 4;
                }
                // Both the "Intel 64 and IA-32 Architectures Developer's Manual: Vol. 2A" and the "AMD CPUID Specification"
                // specify that the Extended Family is defined only when the Base Family is 0Fh.
                // Both manuals define the display family as {0000b,BaseFamily[3:0]} + ExtendedFamily[7:0] in that case.
                if (_family == 0x0F)
                {
                    ext_family = (_f1_EAX >> 20) & 0xFF;
                    _family += ext_family;
                }
            };

            std::string _vendor;
            std::string _brand;

            bool _isIntel;
            bool _isAMD;

            int32_t _family;
            int32_t _model;
            int32_t _stepping;

            int32_t         _f1_EAX;
            std::bitset<32> _f1_ECX;
            std::bitset<32> _f1_EDX;
            std::bitset<32> _f7_EBX;
            std::bitset<32> _f7_ECX;
            std::bitset<32> _f7_EDX;
            int32_t         _fD_EAX;
            int32_t         _fD_EDX;
            std::bitset<32> _f81_EDX;

     private:
            uint32_t _idMax;
            uint32_t _idExtMax;
            std::vector<std::array<int32_t, 4>> _data;
            std::vector<std::array<int32_t, 4>> _dataExt;
        };

    private:
        static void cpuid(int32_t out[4], int32_t eax, int32_t ecx);
        static inline uint64_t xcrFeatureMask() { return ((uint64_t)CPUID._fD_EDX << 32) | CPUID._fD_EAX; } // XCR0 XFEATURE_ENABLED_MASK
    };
} // namespace Stockfish

#endif // #ifndef CPUINFO_H_INCLUDED
