# This script generates Makefile that can be used to import countries
# into libosmscout and generate geocoder-nlp database

import pycountry, os

postal_countries_file = "../postal/countries_languages.txt"
postal_countries = []
for i in open(postal_countries_file, "r"): postal_countries.append(i.split(":")[0])

# print pycountry.countries.lookup("MK")

Name2Country = {
    "azores": "Portugal",
    "bosnia herzegovina": "Bosnia and Herzegovina",
    "great britain": "United Kingdom",
    "macedonia": "MK",
    "russia": "RU",
}

Countries = {
    "europe": [ "albania",
                "andorra",
                "austria",
                "azores",
                "belarus",
                "belgium",
                "bosnia-herzegovina",
                "bulgaria",
                "croatia",
                "cyprus",
                "czech-republic",
                "denmark",
                "estonia",
                "faroe-islands",
                "finland",
                "france",
                "georgia",
                "germany",
                "great-britain",
                "greece",
                "hungary",
                "iceland",
                #"ireland-and-northern-ireland",
                "isle-of-man",
                "italy",
                #"kosovo",
                "latvia",
                "liechtenstein",
                "lithuania",
                "luxembourg",
                "macedonia",
                "malta",
                "moldova",
                "monaco",
                "montenegro",
                "netherlands",
                "norway",
                "poland",
                "portugal",
                "romania",
                "russia",
                "serbia",
                "slovakia",
                "slovenia",
                "spain",
                "sweden",
                "switzerland",
                "turkey",
                "ukraine"
    ]
}

fmake = open("Makefile", "w")
fmake.write("# This Makefile is generated by script\n\n")
fmake.write("BUILDER=./build.sh\n")
fmake.write("WORLD_DIR=world\n")
fmake.write("DOWNLOADS_DIR=downloads\n")
fmake.write("\nall: $(DOWNLOADS_DIR)/.directory $(WORLD_DIR)/all_countries_done\n\techo All Done\n\n")
fmake.write("$(DOWNLOADS_DIR)/.directory:\n\tmkdir -p $(DOWNLOADS_DIR)\n\ttouch $(DOWNLOADS_DIR)/.directory\n\n")

all_countries = ""

def pbfname(continent, country):
    return continent + "-" + country + ".pbf"

def pbfurl(continent, country):
    return "http://download.geofabrik.de/%s/%s-latest.osm.pbf" % (continent, country)

for continent in Countries.keys():
    fmake.write("$(WORLD_DIR)/" + continent + "/.directory:\n\tmkdir -p $(WORLD_DIR)/" + continent + "\n\ttouch $(WORLD_DIR)/" + continent + "/.directory\n\n")

    for country in Countries[continent]:
        country_spaces = country.replace('-', ' ')
        if country_spaces in Name2Country:
            c = pycountry.countries.lookup(Name2Country[country_spaces])
        else:
            c = pycountry.countries.lookup(country_spaces)
        code2 = c.alpha_2
        name = c.name

        print continent, code2, name, (code2.lower() in postal_countries)

        sql = "$(WORLD_DIR)/" + os.path.join(continent, country + ".sqlite.bz2")
        pbf = "$(DOWNLOADS_DIR)/" + pbfname(continent, country)
        all_countries += sql + " "
        fmake.write(sql + ": $(WORLD_DIR)/" + continent + "/.directory " + pbf +
                    "\n\t$(BUILDER) $(DOWNLOADS_DIR)/" + pbfname(continent, country) + " $(WORLD_DIR) " + continent + " " + country + " " + code2 + "\n\n")
        fmake.write(pbf + ":$(DOWNLOADS_DIR)/.directory\n\twget %s -O$(DOWNLOADS_DIR)/%s || (rm -f $(DOWNLOADS_DIR)/%s && exit 1)\n\ttouch $(DOWNLOADS_DIR)/%s\n" %
                    (pbfurl(continent, country), pbfname(continent, country),
                     pbfname(continent, country), pbfname(continent, country)) )

fmake.write("$(WORLD_DIR)/all_countries_done: " + all_countries + "\n\techo > $(WORLD_DIR)/all_countries_done\n\n")

print "\nExamine generated Makefile and run make using it. See build.sh and adjust the used executables first\n"