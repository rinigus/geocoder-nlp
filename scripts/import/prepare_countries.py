# This script generates Makefile that can be used to import countries
# into libosmscout and generate geocoder-nlp database

import pycountry, os, json

postal_countries_file = "../postal/countries_languages.txt"
postal_countries = []
for i in open(postal_countries_file, "r"): postal_countries.append(i.split(":")[0])

Name2Code = {
    "ireland-and-northern-ireland": "GB-IE",
    "north-america/us": "US",
    "haiti-and-domrep": "HT-DO",
    "senegal-and-gambia": "SN-GM",
    "gcc-states": "BH-KW-OM-QA-SA-AE",
    "israel-and-palestine": "IL-PS",
    "malaysia-singapore-brunei": "MY-SG-BN",

    "azores": "PT",
    "bosnia-herzegovina": "BA",
    "great-britain": "GB",
    "kosovo": "RS", # in libpostal dataset
    "macedonia": "MK",
    "russia": "RU",
    "canary-islands": "ES",
    "cape-verde": "CV",
    "comores": "KM",
    "congo-brazzaville": "CG",
    "congo-democratic-republic": "CD",
    "guinea-bissau": "GW",
    "ivory-coast": "CI",
    "saint-helena-ascension-and-tristan-da-cunha": "SH",
    "iran": "IR",
    "north-korea": "KP",
    "south-korea": "KR",
    "syria": "SY",
    "vietnam": "VN",
}

Name2Pretty = {
    "ireland-and-northern-ireland": "Ireland and Northern Ireland",
    "kosovo": "Kosovo",
    "australia-oceania": "Australia and Oceania",
    "north-america/us": "North America/US",
    "haiti-and-domrep": "Haiti and Dominican Republic",
    "senegal-and-gambia": "Senegal and Gambia",
    "gcc-states": "GCC States",
    "israel-and-palestine": "Israel and Palestine",
    "malaysia-singapore-brunei": "Malaysia, Singapore, and Brunei",    
}

NameCapitalize = [
    "azores",
    "north-america/us",
]

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
                "kosovo", 
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
                       "mexico",
                       "us-midwest",
                       "us-northeast",
                       "us-pacific",
                       "us-south",
                       "us-west" ],

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
                          "wyoming" ],

    "south-america": [ "argentina",
                       "bolivia",
                       "brazil",
                       "chile",
                       "colombia",
                       "ecuador",
                       "paraguay",
                       "peru",
                       "suriname",
                       "uruguay", 
    ],
}

fmake = open("Makefile", "w")
fmake.write("# This Makefile is generated by script\n\n")
fmake.write("BUILDER=./build.sh\n")
fmake.write("WORLD_DIR=world\n")
fmake.write("DOWNLOADS_DIR=downloads\n")
fmake.write("\nall: $(DOWNLOADS_DIR)/.directory $(WORLD_DIR)/all_countries_done\n\techo All Done\n\n")
fmake.write("$(DOWNLOADS_DIR)/.directory:\n\tmkdir -p $(DOWNLOADS_DIR)\n\ttouch $(DOWNLOADS_DIR)/.directory\n\n")

all_countries = ""
all_downloads = ""

def pbfname(continent, country):
    cc = continent.replace("/", "-")
    return cc + "-" + country + ".pbf"

def pbfurl(continent, country):
    if country in SpecialURL: return SpecialURL[country]
    return "http://download.geofabrik.de/%s/%s-latest.osm.pbf" % (continent, country)

def namecode(continent, country):
    pretty_name = None
    country_spaces = country.replace('-', ' ')

    if continent in Name2Code: code = Name2Code[continent]
    elif country in Name2Code: code = Name2Code[country]
    elif country.find("us-")==0: code = "US"
    else:
        c = pycountry.countries.lookup(country_spaces)
        code = c.alpha_2
        pretty_name = c.name
    
    if continent in Name2Pretty: pretty_continent = Name2Pretty[continent]
    else:
        pretty_continent = ""
        for c in continent.split('-'):
            pretty_continent += c.capitalize() + " "
        pretty_continent = pretty_continent.strip()
        
    if pretty_name is None:
        if country in Name2Pretty: pretty_name = Name2Pretty[country]
        elif country in NameCapitalize or continent in NameCapitalize or country.find("us-")==0:
            pretty_name = ""
            for c in country_spaces.split():
                pretty_name += c.capitalize() + " "
            pretty_name = pretty_name.strip()
        else:
            c = pycountry.countries.lookup(code)
            pretty_name = c.name
    return code, pretty_continent, pretty_name

########### Main loop #############
provided_countries = {}

for continent in Countries.keys():
    fmake.write("$(WORLD_DIR)/" + continent + "/.directory:\n\tmkdir -p $(WORLD_DIR)/" + continent + "\n\ttouch $(WORLD_DIR)/" + continent + "/.directory\n\n")

    for country in Countries[continent]:

        code2, pretty_continent, pretty_country = namecode(continent, country)

        provided_countries[ pretty_continent + pretty_country ] = { "id": country,
                                                                    "id_continent": continent,
                                                                    "continent": pretty_continent,
                                                                    "name": pretty_country,
                                                                    "postal-country": code2,
                                                                    "osmscout": continent + "/" + country,
                                                                    "postal-country": code2,
                                                                    "geocoder-nlp": os.path.join(continent, country + ".sqlite") }
        
        print continent, country, code2, pretty_continent, pretty_country #, (code2.lower() in postal_countries)

        sql = "$(WORLD_DIR)/" + os.path.join(continent, country + ".sqlite.bz2")
        pbf = "$(DOWNLOADS_DIR)/" + pbfname(continent, country)
        all_countries += sql + " "
        all_downloads += pbf + " "
        fmake.write(sql + ": $(WORLD_DIR)/" + continent + "/.directory " + pbf +
                    "\n\t$(BUILDER) $(DOWNLOADS_DIR)/" + pbfname(continent, country) + " $(WORLD_DIR) " + continent + " " + country + " " + code2 + "\n\n")
        fmake.write(pbf + ":$(DOWNLOADS_DIR)/.directory\n\twget %s -O$(DOWNLOADS_DIR)/%s || (rm -f $(DOWNLOADS_DIR)/%s && exit 1)\n\ttouch $(DOWNLOADS_DIR)/%s\n" %
                    (pbfurl(continent, country), pbfname(continent, country),
                     pbfname(continent, country), pbfname(continent, country)) )

fmake.write("\n$(WORLD_DIR)/all_countries_done: " + all_countries + "\n\techo > $(WORLD_DIR)/all_countries_done\n\n")
fmake.write("download: " + all_downloads + "\n\techo All downloaded\n\n")

# save provided countries
pc = []
keys = provided_countries.keys()
keys.sort()
for k in keys: pc.append(provided_countries[k])
fjson = open("countries.json", "w")
fjson.write( json.dumps( pc, sort_keys=True, indent=4, separators=(',', ': ')) )

print "\nExamine generated Makefile and run make using it. See build.sh and adjust the used executables first\n"
