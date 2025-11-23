#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h> 

#define MSG_TELEMETRIA     1 // Cliente envia estado das cidades
#define MSG_ACK            2 // Confirmação de recebimento
#define MSG_EQUIPE_DRONE   3 // Servidor designa equipe
#define MSG_CONCLUSAO      4 // Cliente informa fim da missão

#define ACK_STATUS_TELEMETRIA    0
#define ACK_STATUS_EQUIPE_DRONE  1
#define ACK_STATUS_CONCLUSAO     2

// Configurações Gerais
#define PORTA_SERVIDOR     8080      
#define MAX_CIDADES        50        
#define BUFFER_SIZE        2048      


typedef struct {
    uint16_t tipo;   
    uint16_t tamanho;
} header_t;

typedef struct {
    int id_cidade;
    int status;    
} telemetria_t;

typedef struct {
    int total;                   
    telemetria_t dados[MAX_CIDADES]; 
} payload_telemetria_t;

typedef struct {
    int status; 
} payload_ack_t;

typedef struct {
    int id_cidade; 
    int id_equipe; 
} payload_equipe_drone_t;

typedef struct {
    int id_cidade; 
    int id_equipe; 
} payload_conclusao_t;

typedef struct {
    telemetria_t cidades[MAX_CIDADES]; 
    int num_cidades;                  
    pthread_mutex_t mutex;           
} cliente_estado_t;

#endif // COMMON_H