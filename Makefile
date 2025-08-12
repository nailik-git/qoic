qoic: qoic.c
	clang -g -fsanitize=address -lpng -o qoic qoic.c
