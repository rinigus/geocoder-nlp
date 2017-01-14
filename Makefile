
######################################################
# Compiler and libraries
CXX := g++

LIBPOSTAL_INCLUDE=-I/usr/local/include
#LIBPOSTAL_LIB=-lpostal
LIBPOSTAL_LIB=-l:libpostal.a -l:libsnappy.a

SQLITE_LIB=-lsqlite3

#CXX_EXTRA_OPTIONS += -DGEONLP_PRINT_DEBUG_QUERIES
#CXX_EXTRA_OPTIONS += -DGEONLP_PRINT_DEBUG

######################################################

APPNAME := geocoder-nlp

SRCSUBDIR := src
OBJSUBDIR := obj

INCLUDE  = $(LIBPOSTAL_INCLUDE) -Ithirdparty/sqlite3pp/headeronly_src -I$(SRCSUBDIR)
LIBRARIES += $(LIBPOSTAL_LIB) $(SQLITE_LIB)

OBJS	= $(patsubst $(SRCSUBDIR)/%.cpp,$(OBJSUBDIR)/%.o,$(wildcard $(SRCSUBDIR)/*.cpp))

CXX_EXTRA_OPTIONS += -std=c++11
CXXFLAGS := -O2 -g $(EXTRA_OPTIONS) $(CXX_EXTRA_OPTIONS) $(INCLUDE)  

AR       = ar 
LD	 = g++ 

all: $(OBJSUBDIR) $(APPNAME)

clean:
	rm -rf core* $(APPNAME) $(OBJSUBDIR)

$(APPNAME): $(OBJS)
	@echo
	@echo "--------- LINKING --- $@ "
	rm -f $(APPNAME)
	$(LD) -o $@ $^ $(LIBRARIES)
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

