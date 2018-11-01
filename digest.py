#!/usr/bin/python
# -*- coding: utf-8 -*-

import hashlib
import sys
import os
import binascii
import json
import struct
import subprocess
import re
from collections import OrderedDict

# Length of checksum footer
ESP_CHECKSUM_SIZE = 1

# Flash sector size, minimum unit of erase.
FLASH_SECTOR_SIZE = 0x1000

# Memory addresses
IROM_MAP_START = 0x40200000
IROM_MAP_END = 0x40300000

def checkFileExists(filename):
    if not os.path.isfile(filename):
        print("Error: File \"%s\" not exists" % filename)
        exit()

def getFWInfoObject(filename):
    command = 'xtensa-lx106-elf-objdump -j ".data" -t %s | grep "fw_info"' % filename
    result = subprocess.check_output(command, shell=True)
    # 3ffe8134 g     O .data  00000068 fw_info
    rs = re.split("\s*", result)
    if len(rs) < 6:
        print("Error: fw_info not found" % filename)
        exit()

    fwinfo_size = int(rs[4], 16)
    fwinfo_start = int(rs[0], 16)

    command = 'xtensa-lx106-elf-objdump -j ".data" -h %s | grep .data' % filename
    result = subprocess.check_output(command, shell=True)
    rs = re.split("\s*", result)
    data_segment_wma = int(rs[4], 16)
    data_segment_offset = int(rs[6], 16)

    command = 'xtensa-lx106-elf-objdump -j ".irom0.text" -h %s | grep .irom0.text' % filename
    result = subprocess.check_output(command, shell=True)
    rs = re.split("\s*", result)
    irom0_segment_wma = int(rs[4], 16)

    return {'fwinfo_start': data_segment_offset +  fwinfo_start - data_segment_wma, 'fwinfo_size': fwinfo_size, 'irom0_addr': (irom0_segment_wma - IROM_MAP_START) & ~(FLASH_SECTOR_SIZE - 1) }


def generateBin(source, target):
    print('Generating final bin: %s' % target)
    command = 'esptool.py elf2image --version=2 --flash_freq 80m --flash_mode dio --flash_size 32m -o %s %s' % (target, source)
    os.system(command)


def main():
    if len(sys.argv) != 3:
        print("Error: Invalid arguments\nUsage: digest.py <elf_file> <bin_file>")
        exit()

    elf_file_name = sys.argv[1]
    bin_file_name = sys.argv[2]

    checkFileExists(elf_file_name)
    checkFileExists(bin_file_name)

    info = getFWInfoObject(elf_file_name)
    fwinfo_size = info['fwinfo_size']
    fwinfo_start = info['fwinfo_start']
    # reading initial FW Info
    with open(elf_file_name, 'r') as f:
        data = f.read()
        fwinfo = data[fwinfo_start: fwinfo_start+fwinfo_size]
    if binascii.hexlify(fwinfo[-36:-32]) != 'ffffffff':
        print("Error: already has digest %s" % bin_file_name)
        exit()

    initial_digest = fwinfo[-32:]

    print('Generating 1st pass bin: %s' % bin_file_name)

    h = hashlib.sha256()
    bin_size = None
    digest_pos = None
    with open(bin_file_name) as f:
        data = f.read()
        bin_size = len(data)
        data = data[:-ESP_CHECKSUM_SIZE]
        fwinfo_pos = data.find( fwinfo )
        if fwinfo_pos == -1:
            print("Error: Can't find fwinfo in %s" % bin_file_name)
            exit()
        digest_pos = fwinfo_pos + fwinfo_size - 32

        h.update(data[:digest_pos - 8])
        h.update(struct.pack('<LL', digest_pos, bin_size))
        h.update(data[digest_pos:])

    digest = h.digest()

    print('Signin digest: %s' % binascii.hexlify(digest))

    with open(elf_file_name, 'r+') as f:
        f.seek(fwinfo_start + fwinfo_size - 32 - 8, 0)
        f.write(struct.pack('<LL', digest_pos, bin_size))
        f.write(digest)
        f.close()

        file_name = os.path.split(bin_file_name)
        ver_list = list(struct.unpack('<BBH8sL', fwinfo[40:56]))
        ver_list[3] = ver_list[3].rstrip('\0')

        bin_info = json.dumps(OrderedDict([
               ('file_mame', file_name[1]),
               ('product', fwinfo[0:40].rstrip('\0')),
               ('version', '%u.%u.%u%s(%u)' % tuple(ver_list) ),
               ('initial_digest', binascii.hexlify(initial_digest) ),
               ('digest'        , binascii.hexlify(digest) ),
               ('digest_pos'    , digest_pos ),
               ('fw_addr'       , '0x%06x' % info['irom0_addr'] ),
               ('fw_info'       , binascii.hexlify(fwinfo[:-40] + struct.pack('<LL', digest_pos, bin_size) + digest) ),
               ('fw_info_start' , fwinfo_pos ),
               ('fw_info_size'  , fwinfo_size ),
               ('bin_size'      , bin_size) ])
               , indent=4, separators=(',', ': '))

        with open(elf_file_name + '.info.json', 'w') as f2:
            f2.write(bin_info)
        
    generateBin(elf_file_name, bin_file_name)
    print(bin_info)


main()