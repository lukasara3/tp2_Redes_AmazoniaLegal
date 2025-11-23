#include "common.h"

// Estrutura interna do servidor para representar uma cidade no Grafo
typedef struct {
    int id;
    char nome[100];
    int tipo; // 0 = Regional, 1 = Capital (tem drone)
} cidade_t;

// Variáveis globais do Servidor
cidade_t cidades[MAX_CIDADES];
int adj[MAX_CIDADES][MAX_CIDADES]; // Matriz de adjacência (distâncias)
int num_cidades;
int num_arestas;

void inicializar_grafo() {
    for (int i = 0; i < MAX_CIDADES; i++) {
        for (int j = 0; j < MAX_CIDADES; j++) {
            adj[i][j] = -1; // -1 indica sem conexão direta
        }
        adj[i][i] = 0; // Distância para si mesmo é 0
    }
}

// Função para remover espaços extras e quebras de linha do final de strings
void trim_newline(char *str) {
    int len = strlen(str);
    while (len > 0 && (str[len-1] == '\n' || str[len-1] == '\r' || str[len-1] == ' ')) {
        str[len-1] = '\0';
        len--;
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
        
        // Lê o ID
        if (fscanf(f, "%d", &id) != 1) break;
        
        // Lê o resto da linha (Nome + Tipo)
        fgets(buffer, sizeof(buffer), f);
        
        // Parsing manual do buffer para separar Nome e Tipo
        // O tipo é o último número da linha.
        int len = strlen(buffer);
        // Achar onde começa o último número (Tipo)
        int k = len - 1;
        while (k >= 0 && (buffer[k] < '0' || buffer[k] > '9')) k--; // Pula espaços/newlines finais
        // Agora k aponta para o último digito do tipo
        // Como o tipo é 0 ou 1, é apenas 1 char. 
        // Mas vamos ser seguros e recuar até o espaço anterior.
        while (k >= 0 && buffer[k] != ' ') k--;
        
        // k aponta para o espaço antes do tipo
        sscanf(&buffer[k+1], "%d", &tipo);
        buffer[k] = '\0'; // Corta a string no espaço antes do tipo
        
        // O que sobrou no buffer é o nome (pode ter espaço inicial)
        char *nome_start = buffer;
        while (*nome_start == ' ') nome_start++; // Pula espaços iniciais
        
        cidades[id].id = id;
        cidades[id].tipo = tipo;
        strncpy(cidades[id].nome, nome_start, 99);
    }

    for (int i = 0; i < num_arestas; i++) {
        int u, v, peso;
        if (fscanf(f, "%d %d %d", &u, &v, &peso) == 3) {
            adj[u][v] = peso;
            adj[v][u] = peso; // Grafo não direcionado
        }
    }

    fclose(f);
    printf("Grafo carregado com sucesso!\n");
}

int main(int argc, char *argv[]) {
    inicializar_grafo();
    
    carregar_grafo("grafo_amazonia_legal.txt");

    printf("\n--- Teste de Leitura ---\n");
    int teste_id = 0;
    printf("Vizinhos de %s (%d):\n", cidades[teste_id].nome, teste_id);
    for(int i=0; i<num_cidades; i++) {
        if(adj[teste_id][i] > 0) {
            printf(" -> %s (Dist: %d km)\n", cidades[i].nome, adj[teste_id][i]);
        }
    }
    printf("------------------------\n\n");

    printf("Servidor pronto (Porta %d). Aguardando mensagens...\n", PORTA_SERVIDOR);
        
    return 0;
}