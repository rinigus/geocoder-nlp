#!/usr/bin/env python3

import sqlite3

db = sqlite3.connect('taginfo-db.db')
c = db.cursor()

mapost = ""
whitelist = ""

keyvals = []
for r in c.execute("select key,value from tags where key='shop' order by count_all desc limit 50"):
    key, value = r

    # no need for these
    if value in ['yes', 'no']:
        continue
    
    keyvals.append([key, value])

keyvals.sort()
for r in keyvals:
    key, value = r

    mapost += '  TYPE ' + key + '_' + value + '\n'
    mapost += '    = NODE AREA ("%s"=="%s")\n' % (key, value)
    mapost += '      {Name, NameAlt}\n      ADDRESS POI\n      GROUP ' + key + '\n\n'

    whitelist += key + '_' + value + '\n'

print(mapost)
print(whitelist)

    
