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
        static std::string Vendor() { return CPUID._vendor; }
        static std::string Brand()  { return CPUID._brand; }
        static std::string get_info_string();

        static bool isIntel() { return CPUID._isIntel; }
        static bool isAMD() { return CPUID._isAMD; }
        static bool isAMDZen3() { return CPUID._isAMD && CPUID._family > 24; }
        static bool detect_OS_AVX();
        static bool detect_OS_AVX512();

        // flags reported by function 0x00000001
        static bool SSE3()        { return CPUID._f1_ECX[0]; }
        static bool PCLMULQDQ()   { return CPUID._f1_ECX[1]; }
        static bool MONITOR()     { return CPUID._f1_ECX[3]; }
        static bool SSSE3()       { return CPUID._f1_ECX[9]; }
        static bool FMA3()        { return CPUID._f1_ECX[12]; }
        static bool CMPXCHG16B()  { return CPUID._f1_ECX[13]; }
        static bool SSE41()       { return CPUID._f1_ECX[19]; }
        static bool SSE42()       { return CPUID._f1_ECX[20]; }
        static bool MOVBE()       { return CPUID._f1_ECX[22]; }
        static bool POPCNT()      { return CPUID._f1_ECX[23]; }
        static bool AES()         { return CPUID._f1_ECX[25]; }
        static bool XSAVE()       { return CPUID._f1_ECX[26]; }
        static bool OSXSAVE()     { return CPUID._f1_ECX[27]; }
        static bool AVX()         { return CPUID._f1_ECX[28]; }
        static bool F16C()        { return CPUID._f1_ECX[29]; }
        static bool RDRAND()      { return CPUID._f1_ECX[30]; }
        static bool MSR()         { return CPUID._f1_EDX[5]; }
        static bool CX8()         { return CPUID._f1_EDX[8]; }
        static bool SEP()         { return CPUID._f1_EDX[11]; }
        static bool CMOV()        { return CPUID._f1_EDX[15]; }
        static bool CLFSH()       { return CPUID._f1_EDX[19]; }
        static bool MMX()         { return CPUID._f1_EDX[23]; }
        static bool FXSR()        { return CPUID._f1_EDX[24]; }
        static bool SSE()         { return CPUID._f1_EDX[25]; }
        static bool SSE2()        { return CPUID._f1_EDX[26]; }
        // flags reported by function 0x00000007
        static bool FSGSBASE()    { return CPUID._f7_EBX[0]; }
        static bool BMI1()        { return CPUID._f7_EBX[3]; }
        static bool HLE()         { return CPUID._isIntel && CPUID._f7_EBX[4]; }
        static bool AVX2()        { return CPUID._f7_EBX[5]; }
        static bool BMI2()        { return CPUID._f7_EBX[8]; }
        static bool ERMS()        { return CPUID._f7_EBX[9]; }
        static bool INVPCID()     { return CPUID._f7_EBX[10]; }
        static bool RTM()         { return CPUID._isIntel && CPUID._f7_EBX[11]; }
        static bool MPX()         { return CPUID._f7_EBX[14]; }
        static bool AVX512F()     { return CPUID._f7_EBX[16]; }
        static bool AVX512DQ()    { return CPUID._f7_EBX[17]; }
        static bool RDSEED()      { return CPUID._f7_EBX[18]; }
        static bool ADX()         { return CPUID._f7_EBX[19]; }
        static bool AVX512IFMA()  { return CPUID._f7_EBX[21]; }
        static bool AVX512PF()    { return CPUID._f7_EBX[26]; }
        static bool AVX512ER()    { return CPUID._f7_EBX[27]; }
        static bool AVX512CD()    { return CPUID._f7_EBX[28]; }
        static bool SHA()         { return CPUID._f7_EBX[29]; }
        static bool AVX512BW()    { return CPUID._f7_EBX[30]; }
        static bool AVX512VL()    { return CPUID._f7_EBX[31]; }
        static bool PREFETCHWT1() { return CPUID._f7_ECX[0]; }
        static bool AVX512VBMI()  { return CPUID._f7_ECX[1]; }
        static bool AVX512VBMI2() { return CPUID._f7_ECX[6]; }
        static bool GFNI()        { return CPUID._f7_ECX[8]; }
        static bool VAES()        { return CPUID._f7_ECX[9]; }
        static bool AVX512VPCLMUL() { return CPUID._f7_ECX[10]; }
        static bool AVX512VNNI()  { return CPUID._f7_ECX[11]; }
        static bool AVX512BITALG() { return CPUID._f7_ECX[12]; }
        static bool AVX512VPOPCNTDQ() { return CPUID._f7_ECX[14]; }
        static bool RDPID()       { return CPUID._f7_ECX[22]; }
        static bool AVX5124FMAPS() { return CPUID._f7_EDX[2]; }
        static bool AVX5124VNNIW() { return CPUID._f7_EDX[3]; }
        // flags reported by extended function 0x80000001
        static bool LAHF()        { return CPUID._f81_ECX[0]; }
        static bool ABM()         { return CPUID._f81_ECX[5]; }
        static bool SSE4a()       { return CPUID._f81_ECX[6]; }
        static bool XOP()         { return CPUID._f81_ECX[11]; }
        static bool FMA4()        { return CPUID._f81_ECX[16]; }
        static bool TBM()         { return CPUID._f81_ECX[21]; }
        static bool SYSCALL()     { return CPUID._isIntel && CPUID._f81_EDX[11]; }
        static bool MMXEXT()      { return CPUID._isAMD && CPUID._f81_EDX[22]; }
        static bool RDTSCP()      { return CPUID._isIntel && CPUID._f81_EDX[27]; }
        static bool X64()         { return CPUID._isIntel && CPUID._f81_EDX[29]; }
        static bool _3DNOWEXT()   { return CPUID._isAMD && CPUID._f81_EDX[30]; }
        static bool _3DNOW()      { return CPUID._isAMD && CPUID._f81_EDX[31]; }

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
                _f1_EBX{ 0 },
                _f1_ECX{ 0 },
                _f1_EDX{ 0 },
                _f7_EAX{ 0 },
                _f7_EBX{ 0 },
                _f7_ECX{ 0 },
                _f7_EDX{ 0 },
                _f81_EAX{ 0 },
                _f81_EBX{ 0 },
                _f81_ECX{ 0 },
                _f81_EDX{ 0 },
                _data{},
                _dataExt{},
                _family{ 0 },
                _model{ 0 },
                _ext_family{ 0 },
                _ext_model{ 0 }
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

                // load bitset with flags for function 0x00000001
                if (_idMax >= 1)
                {
                    _f1_EAX = _data[1][0];
                    _f1_EBX = _data[1][1];
                    _f1_ECX = _data[1][2];
                    _f1_EDX = _data[1][3];
                }

                // load bitset with flags for function 0x00000007
                if (_idMax >= 7)
                {
                    _f7_EAX = _data[7][0];
                    _f7_EBX = _data[7][1];
                    _f7_ECX = _data[7][2];
                    _f7_EDX = _data[7][3];
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
                    _f81_EAX = _dataExt[1][0];
                    _f81_EBX = _dataExt[1][1];
                    _f81_ECX = _dataExt[1][2];
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
                const int32_t signature = _data[1][0];
                _family = (signature >> 8) & 0x0F;
                _model = (signature >> 4) & 0x0F;
                _ext_family = 0;
                _ext_model = 0;
                // The "Intel 64 and IA-32 Architectures Developer's Manual: Vol. 2A"
                // specifies the Extended Model is defined only when the Base Family is
                // 06h or 0Fh.
                // The "AMD CPUID Specification" specifies that the Extended Model is
                // defined only when Base Family is 0Fh.
                // Both manuals define the display model as
                // {ExtendedModel[3:0],BaseModel[3:0]} in that case.
                if (_family == 0x0F || (_family == 0x06 && _isIntel))
                {
                    _ext_model = (signature >> 16) & 0x0F;
                    _model += _ext_model << 4;
                }
                // Both the "Intel 64 and IA-32 Architectures Developer's Manual: Vol. 2A"
                // and the "AMD CPUID Specification" specify that the Extended Family is
                // defined only when the Base Family is 0Fh.
                // Both manuals define the display family as {0000b,BaseFamily[3:0]} +
                // ExtendedFamily[7:0] in that case.
                if (_family == 0x0F)
                {
                    _ext_family = (signature >> 20) & 0xFF;
                    _family += _ext_family;
                }
            };

            uint32_t _idMax;
            uint32_t _idExtMax;
            bool _isIntel;
            bool _isAMD;

            std::bitset<32> _f1_EAX;
            std::bitset<32> _f1_EBX;
            std::bitset<32> _f1_ECX;
            std::bitset<32> _f1_EDX;

            std::bitset<32> _f7_EAX;
            std::bitset<32> _f7_EBX;
            std::bitset<32> _f7_ECX;
            std::bitset<32> _f7_EDX;

            std::bitset<32> _f81_EAX;
            std::bitset<32> _f81_EBX;
            std::bitset<32> _f81_ECX;
            std::bitset<32> _f81_EDX;

            std::vector<std::array<int32_t, 4>> _data;
            std::vector<std::array<int32_t, 4>> _dataExt;

            std::string _vendor;
            std::string _brand;

            int32_t _family;
            int32_t _model;
            int32_t _ext_family;
            int32_t _ext_model;
        };
    private:
        static void cpuid(int32_t out[4], int32_t eax, int32_t ecx);
        static uint64_t xgetbv(unsigned int x);
    };
} // namespace Stockfish

#endif // #ifndef CPUINFO_H_INCLUDED
