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
    "canary islands": "ES",
    "cape verde": "CV",
    "comores": "KM",
    "congo brazzaville": "CG",
    "congo democratic republic": "CD",
    "guinea bissau": "GW",
    "ivory coast": "CI",
    "saint helena ascension and tristan da cunha": "SH",
    "iran": "IR",
    "north korea": "KP",
    "south korea": "KR",
    "syria": "SY",
    "vietnam": "VN",
}

# special maps or countries missing from pycountry
SpecialMaps = { 
    "ireland and northern ireland": "GB-IE",
    "north-america/us": "US",
    "haiti and domrep": "HT-DO",
    "senegal and gambia": "SN-GM",
    "gcc states": "BH-KW-OM-QA-SA-AE",
    "israel and palestine": "IL-PS",
    "malaysia singapore brunei": "my-sg-bn",
}

SpecialURL = {
    "russia": "http://download.geofabrik.de/russia-latest.osm.pbf"
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
                "ireland-and-northern-ireland",
                "isle-of-man",
                "italy",
                #"kosovo", # seems to be missing in libpostal
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
    ],

    "africa": [ "algeria",
                "angola",
                "benin",
                "botswana",
                "burkina-faso",
                "cameroon",
                "canary-islands",
                "cape-verde",
                "central-african-republic",
                "chad",
                "comores",
                "congo-brazzaville",
                "congo-democratic-republic",
                "djibouti",
                "egypt",
                "equatorial-guinea",
                "eritrea",
                "ethiopia",
                "gabon",
                "ghana",
                "guinea",
                "guinea-bissau",
                "ivory-coast",
                "kenya",
                "lesotho",
                "liberia",
                "libya",
                "madagascar",
                "malawi",
                "mali",
                "mauritania",
                "mauritius",
                "morocco",
                "mozambique",
                "namibia",
                "niger",
                "nigeria",
                "rwanda",
                "saint-helena-ascension-and-tristan-da-cunha",
                "sao-tome-and-principe",
                "senegal-and-gambia",
                "seychelles",
                "sierra-leone",
                "somalia",
                "south-africa",
                "south-sudan",
                "sudan",
                "swaziland",
                "tanzania",
                "togo",
                "tunisia",
                "uganda",
                "zambia",
                "zimbabwe" ],

    "asia": [ "afghanistan",
              "azerbaijan",
              "bangladesh",
              "cambodia",
              "gcc-states",
              "china",
              "india",
              "indonesia",
              "japan",
              "iran",
              "iraq",
              "israel-and-palestine",
              "jordan",
              "kazakhstan",
              "kyrgyzstan",
              "lebanon",
              "malaysia-singapore-brunei",
              "maldives",
              "mongolia",
              "nepal",
              "north-korea",
              "pakistan",
              "philippines",
              "south-korea",
              "sri-lanka",
              "syria",
              "taiwan",
              "tajikistan",
              "thailand",
              "turkmenistan",
              "uzbekistan",
              "vietnam",
              "yemen" ], 

    "australia-oceania": [ "australia",
                           "fiji",
                           "new-caledonia",
                           "new-zealand" ],

    "central-america": [ "belize",
                         "cuba",
                         "guatemala",
                         "haiti-and-domrep",
                         "nicaragua" ],

    "north-america": [ "canada",
                       "greenland",
                       "mexico" ],

    "north-america/us": [ "alaska",
                          "alabama",
                          "arizona",
                          "arkansas",
                          "california",
                          "colorado",
                          "connecticut",
                          "delaware",
                          "district-of-columbia",
                          "florida",
                          "georgia",
                          "hawaii",
                          "idaho",
                          "illinois",
                          "indiana",
                          "iowa",
                          "kansas",
                          "kentucky",
                          "louisiana",
                          "maine",
                          "maryland",
                          "massachusetts",
                          "michigan",
                          "minnesota",
                          "mississippi",
                          "missouri",
                          "montana",
                          "nebraska",
                          "nevada",
                          "new-hampshire",
                          "new-jersey",
                          "new-mexico",
                          "new-york",
                          "north-carolina",
                          "north-dakota",
                          "ohio",
                          "oklahoma",
                          "oregon",
                          "pennsylvania",
                          "rhode-island",
                          "south-carolina",
                          "south-dakota",
                          "tennessee",
                          "texas",
                          "utah",
                          "vermont",
                          "virginia",
                          "washington",
                          "west-virginia",
                          "wisconsin",
                          "wyoming",
                          "us-midwest",
                          "us-northeast",
                          "us-pacific",
                          "us-south",
                          "us-west" ],

    "south-america": [ "argentina",
                       "bolivia",
                       "brazil",
                       "chile",
                       "colombia",
                       "ecuador",
                       "paraguay",
                       "peru",
                       "suriname",
                       "uruguay" ],
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
    cc = continent.replace("/", "-")
    return cc + "-" + country + ".pbf"

def pbfurl(continent, country):
    if country in SpecialURL: return SpecialURL[country]
    return "http://download.geofabrik.de/%s/%s-latest.osm.pbf" % (continent, country)

for continent in Countries.keys():
    fmake.write("$(WORLD_DIR)/" + continent + "/.directory:\n\tmkdir -p $(WORLD_DIR)/" + continent + "\n\ttouch $(WORLD_DIR)/" + continent + "/.directory\n\n")

    for country in Countries[continent]:
        country_spaces = country.replace('-', ' ')

        name = country_spaces
        if country_spaces in SpecialMaps:
            code2 = SpecialMaps[country_spaces]
        elif continent in SpecialMaps:
            code2 = SpecialMaps[continent]
        else:
            if country_spaces in Name2Country:
                c = pycountry.countries.lookup(Name2Country[country_spaces])
            else:
                c = pycountry.countries.lookup(country_spaces)
            code2 = c.alpha_2
            name = c.name
        
        print continent, code2, country_spaces, name #, (code2.lower() in postal_countries)

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
