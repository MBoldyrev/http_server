all:
	gcc -std=gnu99 -o server main.c worker.c fd_pass.c -lev -g
