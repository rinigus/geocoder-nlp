
######################################################
# Compiler and libraries
CXX := g++

LIBPOSTAL_INCLUDE=-I/usr/local/include
#LIBPOSTAL_INCLUDE=-I../libpostal-install/include

LIBPOSTAL_LIB=-lpostal
#LIBPOSTAL_LIB=-l:libpostal.a -l:libsnappy.a
#LIBPOSTAL_LIB=-L../libpostal-install/lib -l:libpostal.a 

SQLITE_LIB=-lsqlite3
#SQLITE_LIB=-l:libsqlite3.a

LD_EXTRA_OPTIONS += -pthread -lmarisa -lkyotocabinet
#LD_EXTRA_OPTIONS += -ldl -static-libgcc -static-libstdc++

CXX_EXTRA_OPTIONS += -DGEONLP_PRINT_DEBUG_QUERIES
CXX_EXTRA_OPTIONS += -DGEONLP_PRINT_DEBUG

######################################################

SRCSUBDIR := src
OBJSUBDIR := obj

INCLUDE  = $(LIBPOSTAL_INCLUDE) -Ithirdparty/sqlite3pp/headeronly_src -I$(SRCSUBDIR)
LIBRARIES += $(LIBPOSTAL_LIB) $(SQLITE_LIB)

OBJS	= $(patsubst $(SRCSUBDIR)/%.cpp,$(OBJSUBDIR)/%.o,$(wildcard $(SRCSUBDIR)/*.cpp)) 

CXX_EXTRA_OPTIONS += -std=c++11
CXXFLAGS := -Wall -O2 -g $(EXTRA_OPTIONS) $(CXX_EXTRA_OPTIONS) $(INCLUDE)  

AR       = ar 
LD	 = g++ 

all: $(OBJSUBDIR) geocoder-nlp nearby-line

clean:
	rm -rf core* $(APPNAME) $(OBJSUBDIR)

geocoder-nlp: $(OBJS) $(OBJSUBDIR)/demo_geocoder-nlp.o
	@echo
	@echo "--------- LINKING --- $@ "
	rm -f $(APPNAME)
	$(LD) -o $@ $^ $(LIBRARIES) $(LD_EXTRA_OPTIONS)
	@echo
	@echo '--------- Make done '
	@echo

nearby-line: $(OBJS) $(OBJSUBDIR)/demo_nearby-line.o
	@echo
	@echo "--------- LINKING --- $@ "
	rm -f $(APPNAME)
	$(LD) -o $@ $^ $(LIBRARIES) $(LD_EXTRA_OPTIONS)
	@echo
	@echo '--------- Make done '
	@echo

$(OBJSUBDIR):
	@echo
	@echo "--------- Making dir: $@ "
	mkdir -p $(OBJSUBDIR)
	@echo

$(OBJSUBDIR)/%.o: $(SRCSUBDIR)/%.cpp 
	@echo
	@echo "------------ $< "
	$(CXX) -c $(CPPFLAGS) $(CXXFLAGS) -o $@ $<
	@echo

$(OBJSUBDIR)/demo_geocoder-nlp.o: demo/geocoder-nlp.cpp 
	@echo
	@echo "------------ $< "
	$(CXX) -c $(CPPFLAGS) $(CXXFLAGS) -o $@ $<
	@echo

$(OBJSUBDIR)/demo_nearby-line.o: demo/nearby-line.cpp 
	@echo
	@echo "------------ $< "
	$(CXX) -c $(CPPFLAGS) $(CXXFLAGS) -o $@ $<
	@echo

