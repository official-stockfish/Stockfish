import struct
import sys
import os
import random
from pathlib import Path


def copy_next_chunk(in_file, out_file):
    chunk_header = in_file.read(8)
    assert chunk_header[0:4] == b"BINP"
    size = struct.unpack("<I", chunk_header[4:])[0]

    out_file.write(chunk_header)
    data = in_file.read(size)
    out_file.write(data)

    return size + 8


def main():
    if len(sys.argv) < 4:
        print("Usage: python interleave_binpacks.py infile1 ... infileN outfile")
        print("       The output binpack, will contain all data from the input files.")
        print("       Data is read sequentially from the input, randomly alternating between files.")
        return

    # open last arg as output file name
    out_filename = sys.argv[-1]
    print("outfile: ", out_filename)

    if Path(out_filename).exists():
        print(
            "Output path {} already exists. Please specify a path to a file that does not exist.".format(
                out_filename
            )
        )
        return

    out_file = open(out_filename, "wb")

    # open other args as input file names, and get their sizes
    in_filenames = []
    for i in range(1, len(sys.argv) - 1):
        in_filenames.append(sys.argv[i])
    print("infiles: ", in_filenames)

    in_files = []
    in_files_remaining = []
    for in_filename in in_filenames:
        in_file = open(in_filename, "rb")
        in_files.append(in_file)
        file_size = os.path.getsize(in_filename)
        in_files_remaining.append(file_size)

    # randomly pick a file, with a probability related to their sizes.
    # copy from the front and keep track of remaining sizes
    total_remaining = sum(in_files_remaining)
    print("Merging {} bytes ".format(total_remaining))

    total_size = 0
    report_every = 100
    prev_mib = -report_every

    while total_remaining > 0:
        where = random.randrange(total_remaining)
        i = 0
        while where >= in_files_remaining[i]:
            where -= in_files_remaining[i]
            i += 1
        size = copy_next_chunk(in_files[i], out_file)
        in_files_remaining[i] -= size
        total_remaining -= size
        total_size += size
        mib = total_size // 1024 // 1024
        if mib // 100 != prev_mib // 100:
            print("Copied {} MiB".format(mib))
            prev_mib = mib

    out_file.close()
    for in_file in in_files:
        in_file.close()

    print("Merged  {} bytes".format(total_size))


main()
