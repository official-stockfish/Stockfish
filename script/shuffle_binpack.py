import struct
import sys
import os
import random
from pathlib import Path

def index_binpack(file):
    print('Indexing...')
    index = []
    offset = 0
    report_every = 100
    prev_mib = -report_every
    while file.peek():
        chunk_header = file.read(8)
        assert chunk_header[0:4] == b'BINP'
        size = struct.unpack('<I', chunk_header[4:])[0]
        file.seek(size, os.SEEK_CUR)
        index.append((offset, size + 8))
        offset += size + 8

        mib = offset // 1024 // 1024
        if mib // 100 != prev_mib // 100:
            print('Indexed {} MiB'.format(mib))
            prev_mib = mib

    return index

def copy_binpack_indexed(in_file, index, out_files):
    print('Copying...')
    total_size = 0
    report_every = 100
    prev_mib = -report_every
    nextfile = 0
    for offset, size in index:
        in_file.seek(offset, os.SEEK_SET)
        data = in_file.read(size)
        assert len(data) == size
        out_files[nextfile].write(data)
        nextfile = (nextfile + 1) % len(out_files)

        total_size += size
        mib = total_size // 1024 // 1024
        if mib // 100 != prev_mib // 100:
            print('Copied {} MiB'.format(mib))
            prev_mib = mib

def main():
    if len(sys.argv) < 3:
        print('Usage: python shuffle_binpack.py infile outfile [split_count]')
        return

    in_filename = sys.argv[1]

    if len(sys.argv) > 3:
       # split the infile in split_count pieces, creating new outfile names based on the provided name
       basefile = sys.argv[2]
       split_count = int(sys.argv[3])
       base=os.path.splitext(basefile)[0]
       ext=os.path.splitext(basefile)[1]
       out_filenames = []
       for i in range(split_count):
           out_filenames.append(base+"_{}".format(i)+ext)
    else:
       out_filenames = [sys.argv[2]]

    for out_filename in out_filenames:
      if (Path(out_filename).exists()):
          print('Output path {} already exists. Please specify a path to a file that does not exist.'.format(out_filename))
          return

    print(out_filenames)

    in_file = open(in_filename, 'rb')
    index = index_binpack(in_file)

    print('Shuffling...')
    random.shuffle(index)

    out_files = []
    for out_filename in out_filenames:
        out_files.append(open(out_filename, 'wb'))

    copy_binpack_indexed(in_file, index, out_files)

    in_file.close()
    for out_file in out_files:
        out_file.close()

main()
