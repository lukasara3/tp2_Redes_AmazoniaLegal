#include "common.h"

int main(int argc, char *argv[]) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORTA_SERVIDOR);
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("Endereço inválido");
        exit(EXIT_FAILURE);
    }

    printf("Cliente iniciado. Preparando envio para 127.0.0.1:%d\n", PORTA_SERVIDOR);

    char buffer[sizeof(header_t) + sizeof(payload_telemetria_t)];
    
    header_t *header = (header_t *)buffer;
    payload_telemetria_t *payload = (payload_telemetria_t *)(buffer + sizeof(header_t));

    header->tipo = htons(MSG_TELEMETRIA);
    header->tamanho = htons(sizeof(payload_telemetria_t));

    payload->total = 2; // Fingindo que monitoramos 2 cidades
    payload->dados[0].id_cidade = 0; // Rio Branco
    payload->dados[0].status = 0;    // OK
    payload->dados[1].id_cidade = 25; // Belém
    payload->dados[1].status = 1;     // ALERTA!

    ssize_t sent_bytes = sendto(sockfd, buffer, sizeof(buffer), 0,
                                (struct sockaddr *)&server_addr, sizeof(server_addr));
    
    if (sent_bytes < 0) {
        perror("Erro no envio");
    } else {
        printf("Enviado %ld bytes (Telemetria).\n", sent_bytes);
    }

    printf("Aguardando ACK...\n");
    socklen_t server_len = sizeof(server_addr);
    char recv_buffer[BUFFER_SIZE];
    
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t n = recvfrom(sockfd, recv_buffer, BUFFER_SIZE, 0,
                         (struct sockaddr *)&server_addr, &server_len);

    if (n < 0) {
        perror("Timeout ou erro ao receber ACK");
    } else {
        header_t *recv_header = (header_t *)recv_buffer;
        if (ntohs(recv_header->tipo) == MSG_ACK) {
            printf("Sucesso! ACK recebido do servidor.\n");
        } else {
            printf("Recebido pacote estranho: Tipo %d\n", ntohs(recv_header->tipo));
        }
    }

    close(sockfd);
    return 0;
}