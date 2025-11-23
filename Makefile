CC = gcc
# Flags de compilação:
# -Wall: Mostra todos os avisos (warnings)
# -g: Adiciona info de debug (para usar com GDB/Valgrind)
# -pthread: Habilita a biblioteca POSIX threads
CFLAGS = -Wall -g -pthread

# Targets padrão
all: server client

# Regra para compilar o servidor
server: server.c common.h
	$(CC) $(CFLAGS) server.c -o server

# Regra para compilar o cliente
client: client.c common.h
	$(CC) $(CFLAGS) client.c -o client

# Limpeza dos binários
clean:
	rm -f server client