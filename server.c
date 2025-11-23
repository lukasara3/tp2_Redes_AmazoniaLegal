#include "common.h"

typedef struct {
    int id;
    char nome[100];
    int tipo;
} cidade_t;

cidade_t cidades[MAX_CIDADES];
int adj[MAX_CIDADES][MAX_CIDADES];
int num_cidades;
int num_arestas;

void inicializar_grafo() {
    for (int i = 0; i < MAX_CIDADES; i++) {
        for (int j = 0; j < MAX_CIDADES; j++) {
            adj[i][j] = -1;
        }
        adj[i][i] = 0;
    }
}

void carregar_grafo(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Erro ao abrir arquivo do grafo");
        exit(EXIT_FAILURE);
    }

    if (fscanf(f, "%d %d", &num_cidades, &num_arestas) != 2) {
        fprintf(stderr, "Erro ao ler cabeçalho do grafo\n");
        exit(EXIT_FAILURE);
    }

    printf("Carregando grafo: %d cidades, %d arestas...\n", num_cidades, num_arestas);

    for (int i = 0; i < num_cidades; i++) {
        int id, tipo;
        char buffer[200];

        if (fscanf(f, "%d", &id) != 1) break;

        fgets(buffer, sizeof(buffer), f);

        int len = strlen(buffer);
        int k = len - 1;
        while (k >= 0 && (buffer[k] < '0' || buffer[k] > '9')) k--;
        while (k >= 0 && buffer[k] != ' ') k--;

        sscanf(&buffer[k+1], "%d", &tipo);
        buffer[k] = '\0';

        char *nome_start = buffer;
        while (*nome_start == ' ') nome_start++;

        cidades[id].id = id;
        cidades[id].tipo = tipo;
        strncpy(cidades[id].nome, nome_start, 99);
    }

    for (int i = 0; i < num_arestas; i++) {
        int u, v, peso;
        if (fscanf(f, "%d %d %d", &u, &v, &peso) == 3) {
            adj[u][v] = peso;
            adj[v][u] = peso;
        }
    }

    fclose(f);
    printf("Grafo carregado com sucesso!\n");
}

int main(int argc, char *argv[]) {
    inicializar_grafo();
    carregar_grafo("grafo_amazonia_legal.txt");

    printf("------------------------\n");
    int teste_id = 0;
    printf("Vizinhos de %s (%d):\n", cidades[teste_id].nome, teste_id);
    for(int i=0; i<num_cidades; i++) {
        if(adj[teste_id][i] > 0) {
            printf(" -> %s (Dist: %d km)\n", cidades[i].nome, adj[teste_id][i]);
        }
    }
    printf("------------------------\n\n");

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORTA_SERVIDOR);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro no bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor escutando na porta %d (UDP)...\n", PORTA_SERVIDOR);

    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        ssize_t n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                             (struct sockaddr *)&client_addr, &client_len);

        if (n < 0) {
            perror("Erro ao receber dados");
            continue;
        }

        if (n < sizeof(header_t)) {
            continue;
        }

        header_t *header = (header_t *)buffer;
        uint16_t tipo = ntohs(header->tipo);

        printf("[RECEBIDO] Tipo: %d | Tamanho Payload: %d bytes | De: %s\n",
               tipo, ntohs(header->tamanho), inet_ntoa(client_addr.sin_addr));

        switch (tipo) {
            case MSG_TELEMETRIA: {
                payload_telemetria_t *payload = (payload_telemetria_t *)(buffer + sizeof(header_t));
                printf("  -> Telemetria de %d cidades recebida.\n", payload->total);

                header_t resp_header;
                payload_ack_t resp_payload;

                resp_header.tipo = htons(MSG_ACK);
                resp_header.tamanho = htons(sizeof(payload_ack_t));
                resp_payload.status = ACK_STATUS_TELEMETRIA;

                char resp_buffer[sizeof(header_t) + sizeof(payload_ack_t)];
                memcpy(resp_buffer, &resp_header, sizeof(header_t));
                memcpy(resp_buffer + sizeof(header_t), &resp_payload, sizeof(payload_ack_t));

                sendto(sockfd, resp_buffer, sizeof(resp_buffer), 0,
                       (struct sockaddr *)&client_addr, client_len);
                printf("  <- ACK enviado.\n");
                break;
            }
            default:
                printf("  -> Tipo desconhecido ou não implementado ainda.\n");
        }
        printf("---------------------------------------------------\n");
    }

    close(sockfd);
    return 0;
}