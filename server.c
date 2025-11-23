#include "common.h"
#include <limits.h> // Para INT_MAX

typedef struct {
    int id;
    char nome[100];
    int tipo; // 0 = Regional, 1 = Capital
} cidade_t;

cidade_t cidades[MAX_CIDADES];
int adj[MAX_CIDADES][MAX_CIDADES];
int num_cidades;
int num_arestas;

// Controle de estado das equipes (Índice = ID da cidade/equipe)
// 0 = Livre, 1 = Ocupada
int equipe_ocupada[MAX_CIDADES];

void inicializar_grafo() {
    for (int i = 0; i < MAX_CIDADES; i++) {
        for (int j = 0; j < MAX_CIDADES; j++) {
            adj[i][j] = -1;
        }
        adj[i][i] = 0;
        equipe_ocupada[i] = 0; // Todas livres no inicio
    }
}

void carregar_grafo(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Erro ao abrir arquivo do grafo");
        exit(EXIT_FAILURE);
    }
    if (fscanf(f, "%d %d", &num_cidades, &num_arestas) != 2) exit(EXIT_FAILURE);

    printf("Carregando grafo: %d cidades, %d arestas...\n", num_cidades, num_arestas);

    for (int i = 0; i < num_cidades; i++) {
        int id, tipo;
        char buffer[200];
        if (fscanf(f, "%d", &id) != 1) break;
        fgets(buffer, sizeof(buffer), f);
        
        // Parsing manual do nome/tipo
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
}

// =========================================================
// ALGORITMO DE DIJKSTRA
// Retorna o ID da equipe (Capital) mais proxima disponivel
// =========================================================
int encontrar_drone_mais_proximo(int origem) {
    int dist[MAX_CIDADES];
    int visitado[MAX_CIDADES];
    
    // Inicialização
    for (int i = 0; i < MAX_CIDADES; i++) {
        dist[i] = INT_MAX;
        visitado[i] = 0;
    }
    dist[origem] = 0;

    // Loop principal do Dijkstra
    for (int count = 0; count < num_cidades - 1; count++) {
        // Escolher vértice com menor distância não visitado
        int u = -1;
        int min_val = INT_MAX;
        
        for (int v = 0; v < num_cidades; v++) {
            if (!visitado[v] && dist[v] < min_val) {
                min_val = dist[v];
                u = v;
            }
        }

        if (u == -1) break; // Não há mais caminhos
        visitado[u] = 1;

        // Relaxamento dos vizinhos
        for (int v = 0; v < num_cidades; v++) {
            if (!visitado[v] && adj[u][v] != -1 && dist[u] != INT_MAX &&
                dist[u] + adj[u][v] < dist[v]) {
                dist[v] = dist[u] + adj[u][v];
            }
        }
    }

    // Agora buscamos a Capital mais proxima que esteja LIVRE
    int melhor_equipe = -1;
    int menor_distancia = INT_MAX;

    for (int i = 0; i < num_cidades; i++) {
        // Verifica se é capital (tipo 1) e se a equipe está livre
        if (cidades[i].tipo == 1) {
            if (equipe_ocupada[i] == 0) {
                if (dist[i] < menor_distancia) {
                    menor_distancia = dist[i];
                    melhor_equipe = i;
                }
            }
        }
    }

    if (melhor_equipe != -1) {
        printf("  > Dijkstra: Melhor equipe p/ %s é %s (%d km)\n", 
               cidades[origem].nome, cidades[melhor_equipe].nome, menor_distancia);
    } else {
        printf("  > Dijkstra: Nenhuma equipe disponivel!\n");
    }

    return melhor_equipe;
}

// =========================================================
// MAIN DO SERVIDOR
// =========================================================
int main(int argc, char *argv[]) {
    inicializar_grafo();
    carregar_grafo("grafo_amazonia_legal.txt");

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) exit(EXIT_FAILURE);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORTA_SERVIDOR);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    printf("Servidor pronto na porta %d. Monitorando Amazonia...\n", PORTA_SERVIDOR);

    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        ssize_t n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                             (struct sockaddr *)&client_addr, &client_len);
        if (n < sizeof(header_t)) continue;

        header_t *header = (header_t *)buffer;
        uint16_t tipo = ntohs(header->tipo);

        switch (tipo) {
            // 1. RECEBIMENTO DE TELEMETRIA
            case MSG_TELEMETRIA: {
                payload_telemetria_t *payload = (payload_telemetria_t *)(buffer + sizeof(header_t));
                printf("[TELEMETRIA] Recebido de %s (%d cidades)\n", 
                       inet_ntoa(client_addr.sin_addr), payload->total);

                // Envia ACK imediatamente
                header_t ack_header = { htons(MSG_ACK), htons(sizeof(payload_ack_t)) };
                payload_ack_t ack_payload = { ACK_STATUS_TELEMETRIA };
                char ack_buf[sizeof(header_t) + sizeof(payload_ack_t)];
                memcpy(ack_buf, &ack_header, sizeof(header_t));
                memcpy(ack_buf + sizeof(header_t), &ack_payload, sizeof(payload_ack_t));
                
                sendto(sockfd, ack_buf, sizeof(ack_buf), 0, (struct sockaddr *)&client_addr, client_len);

                // Processar Alertas
                for(int i=0; i < payload->total; i++) {
                    if (payload->dados[i].status == 1) { // ALERTA
                        int id_cidade = payload->dados[i].id_cidade;
                        printf("  ! ALERTA DETECTADO EM: %s (ID %d)\n", cidades[id_cidade].nome, id_cidade);
                        
                        // Rodar Dijkstra
                        int id_equipe = encontrar_drone_mais_proximo(id_cidade);

                        if (id_equipe != -1) {
                            // Marca equipe como ocupada
                            equipe_ocupada[id_equipe] = 1;

                            // Envia ordem de Drone
                            header_t drone_header = { htons(MSG_EQUIPE_DRONE), htons(sizeof(payload_equipe_drone_t)) };
                            payload_equipe_drone_t drone_payload;
                            drone_payload.id_cidade = id_cidade;
                            
                            // *** CORREÇÃO AQUI: removido o label estranho ***
                            drone_payload.id_equipe = id_equipe;

                            char drone_buf[sizeof(header_t) + sizeof(payload_equipe_drone_t)];
                            memcpy(drone_buf, &drone_header, sizeof(header_t));
                            memcpy(drone_buf + sizeof(header_t), &drone_payload, sizeof(payload_equipe_drone_t));

                            sendto(sockfd, drone_buf, sizeof(drone_buf), 0, (struct sockaddr *)&client_addr, client_len);
                            printf("  -> Ordem enviada: Equipe %s despachada.\n", cidades[id_equipe].nome);
                        }
                    }
                }
                break;
            }

            // 2. RECEBIMENTO DE ACK (Do cliente confirmando ordem)
            case MSG_ACK:
                // O enunciado não exige ação específica aqui, apenas logar
                printf("[ACK] Recebido do cliente.\n");
                break;

            // 3. CONCLUSÃO DE MISSÃO
            case MSG_CONCLUSAO: {
                payload_conclusao_t *conclusao = (payload_conclusao_t *)(buffer + sizeof(header_t));
                printf("[CONCLUSAO] Missao em %s finalizada pela equipe %s.\n", 
                       cidades[conclusao->id_cidade].nome, cidades[conclusao->id_equipe].nome);
                
                // Libera a equipe
                equipe_ocupada[conclusao->id_equipe] = 0;
                printf("  -> Equipe %s está LIVRE novamente.\n", cidades[conclusao->id_equipe].nome);

                // Envia ACK de conclusão
                header_t ack_header = { htons(MSG_ACK), htons(sizeof(payload_ack_t)) };
                payload_ack_t ack_payload = { ACK_STATUS_CONCLUSAO };
                char ack_buf[sizeof(header_t) + sizeof(payload_ack_t)];
                memcpy(ack_buf, &ack_header, sizeof(header_t));
                memcpy(ack_buf + sizeof(header_t), &ack_payload, sizeof(payload_ack_t));
                
                sendto(sockfd, ack_buf, sizeof(ack_buf), 0, (struct sockaddr *)&client_addr, client_len);
                break;
            }
        }
        printf("---------------------------------------------------\n");
    }
    close(sockfd);
    return 0;
}