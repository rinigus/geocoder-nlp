#!/usr/bin/env python

import argparse, glob, collections

parser = argparse.ArgumentParser(description='Generate list of countries covered among the addresses')
parser.add_argument('addrdata_dir', type=str,
                    help='path to the directory containing files required for address parser training (formatted_addresses_tagged.random.tsv formatted_places_tagged.random.tsv)')

args = parser.parse_args()

AddrDir = args.addrdata_dir

cntlan = set()
for fname in glob.glob(AddrDir + "/*.tsv"):
    print "Analyzing " + fname
    i = 0
    for l in open(fname, "r"):
        s = l.split()
        k = s[0] + " " + s[1]
        cntlan.add(k)

Countries = collections.defaultdict(list)
while True:
    if len(cntlan) < 1:
        break
    cl = cntlan.pop().split()
    Countries[ cl[1] ].append(cl[0])

keys = Countries.keys()
keys.sort()
for k in keys:
    print k + ": ",
    Countries[k].sort()
    for l in Countries[k]: print l,
    print
    
f = open("countries_languages.txt", "w")
for k in keys:
    f.write(k + ": ")
    for l in Countries[k]: f.write(l + " ")
    f.write("\n")
