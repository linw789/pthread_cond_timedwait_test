CC = gcc
CFLAGS = -g
INCLUDE = -I.
LIBS = -lpthread

amdgpu_cap: main.c
	$(CC) main.c -o cond_wait_test $(CFLAGS) $(INCLUDE) $(LIBS)
