CXX = clang++
CXXFLAGS = -std=c++20 -O0 -Wall -fsanitize=address -fno-omit-frame-pointer -pedantic-errors -g -I include
# CXXFLAGS = -std=c++20 -Wall -pedantic-errors -g -I include
# CXXFLAGS = -std=c++20 -O3 -Wall -pedantic-errors -g -I include
# CXXFLAGS = -std=c++20 -O3 -fprofile-generate -Wall -pedantic-errors -g -I include
# CXXFLAGS = -std=c++20 -O3 -fprofile-use=default.profdata -Wall -pedantic-errors -g -I include

TEST_SRCS = ${wildcard *_test.cpp}
TEST_OBJS = $(addprefix bin/, $(TEST_SRCS:.cpp=.o))
TEST_MAINS = $(addprefix bin/, $(TEST_SRCS:.cpp=))

HEADERS = ${wildcard *.h} cpp_fix_codec/fix.h

SRCS = fix_engine.cpp

OBJS = $(addprefix bin/, $(SRCS:.cpp=.o))

SAMPLE_SERVER = bin/sample_server
SAMPLE_SERVER_OBJ = ${basename ${SAMPLE_SERVER}}.o

SAMPLE_CLIENT = bin/sample_client
SAMPLE_CLIENT_OBJ = ${basename ${SAMPLE_CLIENT}}.o

MAIN = bin/sample_server
MAIN_OBJ = ${basename ${MAIN}}.o

LIB = bin/fix_engine.a
FIX_CODEC = cpp_fix_codec/bin/fix.a

cpp_fix_codec/fix.h:
	git clone https://github.com/robaho/cpp_fix_codec

${FIX_CODEC}:
	cd cpp_fix_codec && make -f Makefile all -j8

.PRECIOUS: bin/%.o

all: ${MAIN} ${SAMPLE_SERVER} ${SAMPLE_CLIENT} $(TEST_MAINS) ${LIB} fixlib
	@echo compile finished

test: ${TEST_MAINS}

run_tests: ${TEST_MAINS}
	for main in $^ ; do \
		$$main; \
	done

${LIB}: ${OBJS}
	ar r ${LIB} ${OBJS}

${MAIN}: ${MAIN_OBJ} ${LIB} ${FIX_CODEC}
	${CXX} ${CXXFLAGS} ${MAIN_OBJ} ${LIB} ${FIX_CODEC} -o ${MAIN}

${SAMPLE_SERVER}: ${SAMPLE_SERVER_OBJ} ${LIB} ${FIX_CODEC}
	${CXX} ${CXXFLAGS} ${SAMPLE_SERVER_OBJ} ${LIB} ${FIX_CODEC} -o ${SAMPLE_SERVER}

${SAMPLE_CLIENT}: ${SAMPLE_CLIENT_OBJ} ${LIB} ${FIX_CODEC}
	${CXX} ${CXXFLAGS} ${SAMPLE_CLIENT_OBJ} ${LIB} ${FIX_CODEC} -o ${SAMPLE_CLIENT}

bin/%_test: bin/%_test.o ${LIB}
	${CXX} ${CXXFLAGS} $@.o ${LIB} -o $@ 

bin/%.o: %.cpp ${HEADERS}
	@ mkdir -p bin
	${CXX} ${CXXFLAGS} -c $(notdir $(basename $@).cpp) -o $@

clean:
	rm -rf bin *~.
	rm -rf cpp_fix_codec

fixlib:
	cd cpp_fix_codec && git pull && make -f Makefile all -j8



