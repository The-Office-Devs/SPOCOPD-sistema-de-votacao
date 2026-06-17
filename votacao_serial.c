#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TOTAL_VOTOS 155000000
#define CANDIDATOS 3

int main() {
    // =========================
    // Marcação de tempo inicial
    // =========================
    struct timespec inicio, fim;
    clock_gettime(CLOCK_MONOTONIC, &inicio);

    // =========================
    // Buffers
    // =========================
    int *todos_votos = malloc(TOTAL_VOTOS * sizeof(int));
    if (todos_votos == NULL) {
        printf("Erro de alocação de memória. Memória insuficiente.\n");
        return 1;
    }

    int global[CANDIDATOS] = {0};

    // =========================
    // Geração de todos os votos
    // =========================
    srand(time(NULL));

    for (int i = 0; i < TOTAL_VOTOS; i++) {
        todos_votos[i] = rand() % CANDIDATOS;
    }
    printf("Gerou %d votos\n", TOTAL_VOTOS);

    // =========================
    // Contagem sequencial
    // =========================
    for (int i = 0; i < TOTAL_VOTOS; i++) {
        global[todos_votos[i]]++;
    }

    // =========================
    // Marcação de tempo final
    // =========================
    clock_gettime(CLOCK_MONOTONIC, &fim);

    // Cálculo do tempo decorrido em segundos
    double tempo_execucao = (fim.tv_sec - inicio.tv_sec) + 
                            (fim.tv_nsec - inicio.tv_nsec) / 1e9;

    // =========================
    // Resultado final
    // =========================
    printf("\n=== Resultado Final (Serial) ===\n");

    int soma = 0;
    for (int i = 0; i < CANDIDATOS; i++) {
        printf("Candidato %d: %d votos\n", i, global[i]);
        soma += global[i];
    }

    printf("\nTotal: %d votos\n", soma);
    printf("Tempo: %.4f segundos\n", tempo_execucao);

    // =========================
    // Liberação de memória
    // =========================
    free(todos_votos);

    return 0;
}