# Importing and processing libpostal for country specific datasets

When run from the folder containing geocoder-nlp and training data in separate subfolders.

* Split addresses by
```
geocoder-nlp/scripts/postal/split_addresses.py training
```


* To test training on a single country, run
```
geocoder-nlp/scripts/postal/build_country_db.sh training countries EE
```


* Make countries and languages list using
```
geocoder-nlp/scripts/postal/make_country_list.py training
```

* Generate Makefile to run address parser generators
```
geocoder-nlp/scripts/postal/build_all_country_db.py training countries
```

* Run address parser generator via `make -j 16`

Note that if address parser is interrupted, Makefile has to be
generated again to remove already generated parts. It doesn't support
such stops and restarts. 
