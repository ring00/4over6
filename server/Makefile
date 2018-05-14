all: server.exe

run: server.exe
	sudo ./server.exe

server.exe: server.c
	gcc server.c -o server.exe -O2 -lpthread
