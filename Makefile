all:http-server
	@echo "finish make"
clean:
	rm -fv *.o http-server
http-server: http-server.c
	gcc -o $@ $^ -I. -L. -lssl -lcrypto