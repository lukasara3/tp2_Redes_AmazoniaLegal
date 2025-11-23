#include "common.h"
#include <time.h>
#include <errno.h>

// =========================================================
// VARIÁVEIS GLOBAIS E SINCRONIZAÇÃO
// =========================================================

// Dados das cidades (Carregados do arquivo para log local)
typedef struct {
    int id;
    char nome[100];
} cidade_local_t;

cidade_local_t lista_cidades[MAX_CIDADES];
int total_cidades_carregadas = 0;

// Estado Compartilhado (Monitoramento)
cliente_estado_t estado_monitoramento;

// Controle do Drone
int drone_ocupado = 0;              // 0 = Livre, 1 = Em Missão
int missao_pendente_id_cidade = -1; // ID da cidade para a missão atual
int missao_pendente_id_equipe = -1; // ID da equipe que está atuando

// Flags de Comunicação entre Threads
int ack_telemetria_recebido = 0;
int missao_concluida = 0;           // Flag para Thread 4 avisar Thread 3

// Sincronização
pthread_mutex_t mutex_dados = PTHREAD_MUTEX_INITIALIZER;    // Protege dados de telemetria
pthread_mutex_t mutex_controle = PTHREAD_MUTEX_INITIALIZER; // Protege flags de drone/ack

pthread_cond_t cond_ack_telemetria = PTHREAD_COND_INITIALIZER; // Thread 3 acorda Thread 2
pthread_cond_t cond_inicio_missao = PTHREAD_COND_INITIALIZER;  // Thread 3 acorda Thread 4

// Rede
int sockfd;
struct sockaddr_in server_addr;

// =========================================================
// LEITURA DO ARQUIVO (Simplificada para o Cliente)
// =========================================================
void carregar_cidades_cliente(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) { perror("Erro arquivo"); exit(1); }
    
    int num_arestas_ignore;
    if(fscanf(f, "%d %d", &total_cidades_carregadas, &num_arestas_ignore) != 2) exit(1);
    
    for (int i = 0; i < total_cidades_carregadas; i++) {
        int id;
        char buffer[200];
        if(fscanf(f, "%d", &id) != 1) break;
        fgets(buffer, sizeof(buffer), f); // Lê resto da linha (Nome + Tipo)
        
        // Remove o tipo (último dígito) e espaços extras para pegar só o nome
        int len = strlen(buffer);
        int k = len - 1;
        while (k >= 0 && (buffer[k] < '0' || buffer[k] > '9')) k--; // Pula quebras de linha
        while (k >= 0 && buffer[k] != ' ') k--; // Volta até o espaço antes do tipo
        buffer[k] = '\0'; // Corta a string
        
        char *start = buffer;
        while (*start == ' ') start++; // Pula espaços iniciais

        lista_cidades[id].id = id;
        strncpy(lista_cidades[id].nome, start, 99);
        
        // Inicializa estado no vetor global de monitoramento
        estado_monitoramento.cidades[i].id_cidade = id;
        estado_monitoramento.cidades[i].status = 0;
    }
    estado_monitoramento.num_cidades = total_cidades_carregadas;
    fclose(f);
}

// =========================================================
// THREAD 1: MONITORAMENTO (Simula sensores)
// =========================================================
void *thread_monitoramento(void *arg) {
    printf("[Thread 1] Monitoramento iniciado.\n");
    while (1) {
        pthread_mutex_lock(&mutex_dados);
        
        // Simula leitura de sensores para todas as cidades
        for (int i = 0; i < estado_monitoramento.num_cidades; i++) {
            // 1% de chance de gerar alerta por cidade a cada ciclo
            // (Baixei para 1% para não floodar de alertas no teste)
            int r = rand() % 100;
            if (r < 1) { 
                estado_monitoramento.cidades[i].status = 1; // ALERTA
            } else {
                // Mantém o alerta anterior se já estava (ou reseta, depende da lógica de sensor)
                // O enunciado diz "gerar valores", então vamos sobrescrever:
                estado_monitoramento.cidades[i].status = 0; 
            }
        }
        
        pthread_mutex_unlock(&mutex_dados);
        sleep(1); // Coleta a cada segundo
    }
    return NULL;
}

// =========================================================
// THREAD 2: ENVIO DE TELEMETRIA
// =========================================================
void *thread_telemetria(void *arg) {
    printf("[Thread 2] Envio de Telemetria iniciado.\n");
    
    while (1) {
        // O enunciado pede 30 segundos. Para teste, sugiro mudar para 5.
        // sleep(30); 
        sleep(5); 

        printf("\n[TELEMETRIA] Preparando envio...\n");

        // 1. Preparar Payload (Cópia protegida por Mutex)
        char buffer[sizeof(header_t) + sizeof(payload_telemetria_t)];
        header_t *header = (header_t *)buffer;
        payload_telemetria_t *payload = (payload_telemetria_t *)(buffer + sizeof(header_t));

        pthread_mutex_lock(&mutex_dados);
        payload->total = estado_monitoramento.num_cidades;
        int alertas_cont = 0;
        for (int i = 0; i < payload->total; i++) {
            payload->dados[i] = estado_monitoramento.cidades[i];
            if(payload->dados[i].status == 1) {
                printf("  ! Alerta em: %s\n", lista_cidades[payload->dados[i].id_cidade].nome);
                alertas_cont++;
            }
        }
        pthread_mutex_unlock(&mutex_dados);

        if (alertas_cont == 0) printf("  (Nenhum alerta neste ciclo)\n");

        header->tipo = htons(MSG_TELEMETRIA);
        header->tamanho = htons(sizeof(payload_telemetria_t));

        // 2. Lógica de Envio com Tentativas
        int tentativas = 0;
        int sucesso = 0;

        while (tentativas < 3 && !sucesso) {
            tentativas++;
            // printf("  -> Enviando pacote (Tentativa %d/3)...\n", tentativas);
            
            sendto(sockfd, buffer, sizeof(buffer), 0, 
                   (struct sockaddr *)&server_addr, sizeof(server_addr));

            // Esperar pelo ACK (Sinalizado pela Thread 3)
            pthread_mutex_lock(&mutex_controle);
            ack_telemetria_recebido = 0; // Reset flag antes de esperar

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 5; // Timeout de 5 segundos

            // Espera condicional com timeout (libera o mutex enquanto espera)
            int rc = pthread_cond_timedwait(&cond_ack_telemetria, &mutex_controle, &ts);

            if (ack_telemetria_recebido) {
                // printf("  -> ACK confirmado.\n");
                sucesso = 1;
            } else if (rc == ETIMEDOUT) {
                printf("  [TIMEOUT] Sem ACK do servidor na tentativa %d.\n", tentativas);
            }
            pthread_mutex_unlock(&mutex_controle);
        }
        
        if (!sucesso) printf("  [FALHA] Servidor não respondeu após 3 tentativas.\n");
    }
    return NULL;
}

// =========================================================
// THREAD 4: SIMULAÇÃO DE DRONES (Trabalhador)
// =========================================================
void *thread_drone(void *arg) {
    printf("[Thread 4] Simulador de Drones pronto.\n");
    
    while (1) {
        pthread_mutex_lock(&mutex_controle);
        // Espera passiva: só acorda se receber sinal da Thread 3
        while (!drone_ocupado) {
            pthread_cond_wait(&cond_inicio_missao, &mutex_controle);
        }
        
        int id_c = missao_pendente_id_cidade;
        int id_e = missao_pendente_id_equipe;
        pthread_mutex_unlock(&mutex_controle);

        printf("\n>>> [DRONE] Missao INICIADA! Cidade: %s | Equipe: %s\n", 
               lista_cidades[id_c].nome, lista_cidades[id_e].nome);
        
        // Simula tempo de voo (aleatório entre 5 e 10s para teste)
        int tempo_voo = (rand() % 6) + 5; 
        printf("    (Duração estimada: %d segundos...)\n", tempo_voo);
        sleep(tempo_voo);

        printf(">>> [DRONE] Missao CONCLUIDA!\n");

        // Avisa a Thread 3 que acabou para ela enviar a MSG_CONCLUSAO
        pthread_mutex_lock(&mutex_controle);
        missao_concluida = 1; // Flag levantada
        pthread_mutex_unlock(&mutex_controle);
        
        // Pequena pausa para garantir que Thread 3 pegue a flag antes de resetarmos
        // (Na implementação ideal, Thread 3 resetaria drone_ocupado, mas vamos simplificar)
        while(missao_concluida) {
            usleep(100000); // 100ms polling esperando Thread 3 enviar o pacote
        }
    }
    return NULL;
}

// =========================================================
// THREAD 3: RECEPÇÃO E GERENCIAMENTO (Ouvido da Rede)
// =========================================================
void *thread_recepcao(void *arg) {
    printf("[Thread 3] Recepcao UDP iniciada.\n");
    
    char buffer[BUFFER_SIZE];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    // Timeout curto no socket para permitir checar flags periodicamente
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000; // 200ms
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (1) {
        // 1. Verificar se Thread 4 terminou uma missão
        pthread_mutex_lock(&mutex_controle);
        if (missao_concluida) {
            printf("[CLIENTE] Enviando MSG_CONCLUSAO ao servidor...\n");
            
            char msg_buf[sizeof(header_t) + sizeof(payload_conclusao_t)];
            header_t *head = (header_t *)msg_buf;
            payload_conclusao_t *pay = (payload_conclusao_t *)(msg_buf + sizeof(header_t));

            head->tipo = htons(MSG_CONCLUSAO);
            head->tamanho = htons(sizeof(payload_conclusao_t));
            pay->id_cidade = missao_pendente_id_cidade;
            pay->id_equipe = missao_pendente_id_equipe;

            sendto(sockfd, msg_buf, sizeof(msg_buf), 0, 
                   (struct sockaddr *)&server_addr, sizeof(server_addr));

            // Reseta estado do drone
            drone_ocupado = 0; 
            missao_concluida = 0; // Libera Thread 4 para próxima
        }
        pthread_mutex_unlock(&mutex_controle);

        // 2. Receber dados da Rede
        ssize_t n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, 
                             (struct sockaddr *)&sender_addr, &sender_len);
        
        if (n < 0) continue; // Timeout ou erro, volta pro loop
        if (n < sizeof(header_t)) continue;

        header_t *header = (header_t *)buffer;
        uint16_t tipo = ntohs(header->tipo);

        switch (tipo) {
            case MSG_ACK: {
                payload_ack_t *pay = (payload_ack_t *)(buffer + sizeof(header_t));
                if (pay->status == ACK_STATUS_TELEMETRIA) {
                    // Avisa Thread 2
                    pthread_mutex_lock(&mutex_controle);
                    ack_telemetria_recebido = 1;
                    pthread_cond_signal(&cond_ack_telemetria);
                    pthread_mutex_unlock(&mutex_controle);
                }
                else if (pay->status == ACK_STATUS_CONCLUSAO) {
                    printf("  -> Servidor confirmou fim da missao.\n");
                }
                break;
            }

            case MSG_EQUIPE_DRONE: {
                payload_equipe_drone_t *pay = (payload_equipe_drone_t *)(buffer + sizeof(header_t));
                printf("\n[ORDEM RECEBIDA] Equipe %d designada para cidade %d\n", 
                       pay->id_equipe, pay->id_cidade);

                // Envia ACK da ordem (Protocolo)
                header_t h_ack = { htons(MSG_ACK), htons(sizeof(payload_ack_t)) };
                payload_ack_t p_ack = { ACK_STATUS_EQUIPE_DRONE };
                char b_ack[sizeof(header_t) + sizeof(payload_ack_t)];
                memcpy(b_ack, &h_ack, sizeof(header_t));
                memcpy(b_ack + sizeof(header_t), &p_ack, sizeof(payload_ack_t));
                sendto(sockfd, b_ack, sizeof(b_ack), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

                pthread_mutex_lock(&mutex_controle);
                if (drone_ocupado) {
                    printf("  [AVISO] Drone já está ocupado! Ordem ignorada.\n");
                } else {
                    // Aciona Thread 4
                    drone_ocupado = 1;
                    missao_pendente_id_cidade = pay->id_cidade;
                    missao_pendente_id_equipe = pay->id_equipe;
                    pthread_cond_signal(&cond_inicio_missao);
                }
                pthread_mutex_unlock(&mutex_controle);
                break;
            }
        }
    }
    return NULL;
}

// =========================================================
// MAIN
// =========================================================
int main(int argc, char *argv[]) {
    srand(time(NULL));

    // 1. Carregar Cidades para memória
    carregar_cidades_cliente("grafo_amazonia_legal.txt");
    printf("Cliente carregou %d cidades.\n", estado_monitoramento.num_cidades);

    // 2. Configurar Rede
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) exit(1);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORTA_SERVIDOR);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr); // Localhost

    // 3. Iniciar Threads
    pthread_t t1, t2, t3, t4;
    
    // Cria as 4 threads conforme enunciado
    pthread_create(&t1, NULL, thread_monitoramento, NULL); // Gera dados
    pthread_create(&t2, NULL, thread_telemetria, NULL);    // Envia dados
    pthread_create(&t3, NULL, thread_recepcao, NULL);      // Recebe dados
    pthread_create(&t4, NULL, thread_drone, NULL);         // Simula drone

    // Aguarda threads (não deve retornar nunca)
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    pthread_join(t4, NULL);

    close(sockfd);
    return 0;
}