build_and_run:
	@lex -o lexer.cpp lexer.l
	@clang++-17 -g -O3 main.cpp lexer.cpp `llvm-config-17 --cxxflags --ldflags --system-libs --libs core` -o main -ll
	@#./main

clean:
	@rm -f lexer.cpp main output.ll

# Run this command in the terminal
# ./main; lli-17 output.ll; echo "Result is: $?"