#!/usr/bin/env python

import argparse, glob, collections, shutil, os

parser = argparse.ArgumentParser(description='Split addresses databases by countries')
parser.add_argument('addrdata_dir', type=str,
                    help='path to the directory containing files required for address parser training (formatted_addresses_tagged.random.tsv formatted_places_tagged.random.tsv)')

args = parser.parse_args()

AddrDir = args.addrdata_dir
SplitDir = os.path.join(AddrDir, "split")

def splitname(fname, c):
    b = os.path.basename(fname)
    d = fname + "-split"
    f = os.path.join(d, b + "-" + c)
    return d, f

def write(fname_base, data, c):
    cnt = data[c]
    d, fname = splitname(fname_base, c)
    f = open(fname, "a")
    while True:
        if len(cnt) < 1:
            break
        l = cnt.pop()
        f.write(l)
    data[c] = collections.deque()

for fname in glob.glob(AddrDir + "/*.tsv"):
    print "Splitting " + fname
            
    d, f = splitname(fname, "not-important")
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(d)
    print "Directory containing split files:", d
    
    data = collections.defaultdict(collections.deque)
    for l in open(fname, "r"):
        c = l.split()[1]

        data[c].append(l)
        if len(data[c]) > 1000:
            write(fname, data, c)

    for k in data.keys():
        write(fname, data, k)
        
