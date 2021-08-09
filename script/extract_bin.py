import sys

ENTRY_SIZE = 40
NUM_ENTRIES_IN_CHUNK = 1024*1024

def copy(infile, outfile, count, times):
    if times > 1:
        outfile.write(infile.read(count*ENTRY_SIZE)*times)
    else:
        offset = 0
        while offset < count:
            to_read = NUM_ENTRIES_IN_CHUNK if offset + NUM_ENTRIES_IN_CHUNK <= count else count - offset

            outfile.write(infile.read(to_read*ENTRY_SIZE))

            offset += NUM_ENTRIES_IN_CHUNK

def work():
    filename = sys.argv[1]
    offset = int(sys.argv[2])
    count = int(sys.argv[3])
    times = int(sys.argv[4]) if len(sys.argv) >= 5 else 1

    with open(filename, 'rb') as infile:
        infile.seek(offset * ENTRY_SIZE)
        filename_parts = filename.split('.')
        out_path = '.'.join(filename_parts[:-1]) + '_' + str(offset) + '_' + str(count) + '_' + str(times) + '.' + filename_parts[-1]
        with open(out_path, 'wb') as outfile:
            copy(infile, outfile, count, times)

def show_help():
    print('Usage: python extract_bin.py filename offset count [times]')
    print('filename - the path to the .bin file to process')
    print('offset - the number of sfens to skip')
    print('count - the number of sfens to extract')
    print('times - the number of times to repeat the extracted sfens. Default = 1')
    print('The result is saved in a new file named `filename.stem`_`offset`_`count`_`times`.bin')

if len(sys.argv) < 4:
    show_help()
else:
    work()
