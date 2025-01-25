CXX = clang++
INCLUDES = -I ../cpp_fix_codec -I ../cpp_fixed

# CXXFLAGS = -std=c++20 -O0 -Wall -fsanitize=address -fno-omit-frame-pointer -pedantic-errors -g ${INCLUDES}
# CXXFLAGS = -std=c++20 -Wall -pedantic-errors -g ${INCLUDES}
CXXFLAGS = -std=c++20 -O3 -Wall -pedantic-errors -g ${INCLUDES}
# CXXFLAGS = -std=c++20 -O3 -fprofile-generate -Wall -pedantic-errors -g ${INCLUDES}
# CXXFLAGS = -std=c++20 -O3 -fprofile-use=default.profdata -Wall -pedantic-errors -g ${INCLUDES}

TEST_SRCS = ${wildcard *_test.cpp}
TEST_OBJS = $(addprefix bin/, $(TEST_SRCS:.cpp=.o))
TEST_MAINS = $(addprefix bin/, $(TEST_SRCS:.cpp=))

SAMPLE_SRCS = ${wildcard sample_*.cpp}
SAMPLE_OBJS = $(addprefix bin/, $(SAMPLE_SRCS:.cpp=.o))
SAMPLE_MAINS = $(addprefix bin/, $(SAMPLE_SRCS:.cpp=))

HEADERS = ${wildcard *.h}

SRCS = fix_engine.cpp
OBJS = $(addprefix bin/, $(SRCS:.cpp=.o))

LIB = bin/fix_engine.a
FIX_CODEC = ../cpp_fix_codec/bin/fix_codec.a

.PRECIOUS: bin/%.o

all: ${SAMPLE_MAINS} $(TEST_MAINS) ${LIB}
	@echo compile finished

test: ${TEST_MAINS}

run_tests: ${TEST_MAINS}
	for main in $^ ; do \
		$$main; \
	done

${LIB}: ${OBJS}
	ar r ${LIB} ${OBJS}

${SAMPLE_MAINS}: ${SAMPLE_OBJS} ${LIB} ${FIX_CODEC}
	${CXX} ${CXXFLAGS} $@.o ${LIB} ${FIX_CODEC} -o $@

bin/%_test: bin/%_test.o ${LIB} ${FIX_CODEC}
	${CXX} ${CXXFLAGS} $@.o ${LIB} ${FIX_CODEC} -o $@ 

bin/%.o: %.cpp ${HEADERS}
	@ mkdir -p bin
	${CXX} ${CXXFLAGS} -c $(notdir $(basename $@).cpp) -o $@

clean:
	rm -rf bin *~.




