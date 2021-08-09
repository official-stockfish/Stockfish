#ifndef _SFEN_STREAM_H_
#define _SFEN_STREAM_H_

#include "packed_sfen.h"

#include "extra/nnue_data_binpack_format.h"

#include <optional>
#include <fstream>
#include <string>
#include <memory>

namespace Stockfish::Tools {

    enum struct SfenOutputType
    {
        Bin,
        Binpack
    };

    static bool ends_with(const std::string& lhs, const std::string& end)
    {
        if (end.size() > lhs.size()) return false;

        return std::equal(end.rbegin(), end.rend(), lhs.rbegin());
    }

    static bool has_extension(const std::string& filename, const std::string& extension)
    {
        return ends_with(filename, "." + extension);
    }

    static std::string filename_with_extension(const std::string& filename, const std::string& ext)
    {
        if (ends_with(filename, ext))
        {
            return filename;
        }
        else
        {
            return filename + "." + ext;
        }
    }

    struct BasicSfenInputStream
    {
        virtual std::optional<PackedSfenValue> next() = 0;
        virtual bool eof() const = 0;
        virtual ~BasicSfenInputStream() {}
    };

    struct BinSfenInputStream : BasicSfenInputStream
    {
        static constexpr auto openmode = std::ios::in | std::ios::binary;
        static inline const std::string extension = "bin";

        BinSfenInputStream(std::string filename) :
            m_stream(filename, openmode),
            m_eof(!m_stream)
        {
        }

        std::optional<PackedSfenValue> next() override
        {
            PackedSfenValue e;
            if(m_stream.read(reinterpret_cast<char*>(&e), sizeof(PackedSfenValue)))
            {
                return e;
            }
            else
            {
                m_eof = true;
                return std::nullopt;
            }
        }

        bool eof() const override
        {
            return m_eof;
        }

        ~BinSfenInputStream() override {}

    private:
        std::fstream m_stream;
        bool m_eof;
    };

    struct BinpackSfenInputStream : BasicSfenInputStream
    {
        static constexpr auto openmode = std::ios::in | std::ios::binary;
        static inline const std::string extension = "binpack";

        BinpackSfenInputStream(std::string filename) :
            m_stream(filename, openmode),
            m_eof(!m_stream.hasNext())
        {
        }

        std::optional<PackedSfenValue> next() override
        {
            static_assert(sizeof(binpack::nodchip::PackedSfenValue) == sizeof(PackedSfenValue));

            if (!m_stream.hasNext())
            {
                m_eof = true;
                return std::nullopt;
            }

            auto training_data_entry = m_stream.next();
            auto v = binpack::trainingDataEntryToPackedSfenValue(training_data_entry);
            PackedSfenValue psv;
            // same layout, different types. One is from generic library.
            std::memcpy(&psv, &v, sizeof(PackedSfenValue));

            return psv;
        }

        bool eof() const override
        {
            return m_eof;
        }

        ~BinpackSfenInputStream() override {}

    private:
        binpack::CompressedTrainingDataEntryReader m_stream;
        bool m_eof;
    };

    struct BasicSfenOutputStream
    {
        virtual void write(const PSVector& sfens) = 0;
        virtual ~BasicSfenOutputStream() {}
    };

    struct BinSfenOutputStream : BasicSfenOutputStream
    {
        static constexpr auto openmode = std::ios::out | std::ios::binary | std::ios::app;
        static inline const std::string extension = "bin";

        BinSfenOutputStream(std::string filename) :
            m_stream(filename_with_extension(filename, extension), openmode)
        {
        }

        void write(const PSVector& sfens) override
        {
            m_stream.write(reinterpret_cast<const char*>(sfens.data()), sizeof(PackedSfenValue) * sfens.size());
        }

        ~BinSfenOutputStream() override {}

    private:
        std::fstream m_stream;
    };

    struct BinpackSfenOutputStream : BasicSfenOutputStream
    {
        static constexpr auto openmode = std::ios::out | std::ios::binary | std::ios::app;
        static inline const std::string extension = "binpack";

        BinpackSfenOutputStream(std::string filename) :
            m_stream(filename_with_extension(filename, extension), openmode)
        {
        }

        void write(const PSVector& sfens) override
        {
            static_assert(sizeof(binpack::nodchip::PackedSfenValue) == sizeof(PackedSfenValue));

            for(auto& sfen : sfens)
            {
                // The library uses a type that's different but layout-compatibile.
                binpack::nodchip::PackedSfenValue e;
                std::memcpy(&e, &sfen, sizeof(binpack::nodchip::PackedSfenValue));
                m_stream.addTrainingDataEntry(binpack::packedSfenValueToTrainingDataEntry(e));
            }
        }

        ~BinpackSfenOutputStream() override {}

    private:
        binpack::CompressedTrainingDataEntryWriter m_stream;
    };

    inline std::unique_ptr<BasicSfenInputStream> open_sfen_input_file(const std::string& filename)
    {
        if (has_extension(filename, BinSfenInputStream::extension))
            return std::make_unique<BinSfenInputStream>(filename);
        else if (has_extension(filename, BinpackSfenInputStream::extension))
            return std::make_unique<BinpackSfenInputStream>(filename);

        return nullptr;
    }

    inline std::unique_ptr<BasicSfenOutputStream> create_new_sfen_output(const std::string& filename, SfenOutputType sfen_output_type)
    {
        switch(sfen_output_type)
        {
            case SfenOutputType::Bin:
                return std::make_unique<BinSfenOutputStream>(filename);
            case SfenOutputType::Binpack:
                return std::make_unique<BinpackSfenOutputStream>(filename);
        }

        assert(false);
        return nullptr;
    }

    inline std::unique_ptr<BasicSfenOutputStream> create_new_sfen_output(const std::string& filename)
    {
        if (has_extension(filename, BinSfenOutputStream::extension))
            return std::make_unique<BinSfenOutputStream>(filename);
        else if (has_extension(filename, BinpackSfenOutputStream::extension))
            return std::make_unique<BinpackSfenOutputStream>(filename);

        return nullptr;
    }
}

#endif