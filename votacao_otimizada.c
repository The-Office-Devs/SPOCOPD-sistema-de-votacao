#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TOTAL_VOTOS 155000000
#define CANDIDATOS 3

int main(int argc, char *argv[])
{
    int rank, size;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    double inicio = MPI_Wtime();

    int votos_por_processo = TOTAL_VOTOS / size;
    int resto = TOTAL_VOTOS % size;

    int local[CANDIDATOS] = {0};
    int global[CANDIDATOS] = {0};

    // =======================================================
    // 1. SEMENTE DISTRIBUÍDA
    // =======================================================
    srand(time(NULL) + (rank * 1000));

    // =======================================================
    // 2. GERAÇÃO E CONTAGEM SIMULTÂNEA
    // =======================================================
    for (int i = 0; i < votos_por_processo; i++)
    {
        int voto = rand() % CANDIDATOS;
        local[voto]++;
    }

    // =======================================================
    // 3. TRATAMENTO DO RESTO
    // =======================================================
    if (rank == 0 && resto > 0)
    {
        for (int i = 0; i < resto; i++)
        {
            int voto = rand() % CANDIDATOS;
            local[voto]++;
        }
    }

    // =======================================================
    // 4. REPLICAÇÃO DA CONTAGEM 
    // =======================================================
    int backup[CANDIDATOS] = {0};

    if (rank % 2 == 0)
    {
        if (rank + 1 < size)
        {
            MPI_Send(
                local,
                CANDIDATOS,
                MPI_INT,
                rank + 1,
                0,
                MPI_COMM_WORLD);
        }
    }
    else
    {
        MPI_Recv(
            backup,
            CANDIDATOS,
            MPI_INT,
            rank - 1,
            0,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE);
    }

    // =======================================================
    // 5. REDUÇÃO PRINCIPAL
    // =======================================================
    MPI_Reduce(
        local,
        global,
        CANDIDATOS,
        MPI_INT,
        MPI_SUM,
        0,
        MPI_COMM_WORLD);

    // =======================================================
    // 6. CONSENSO BÁSICO
    // =======================================================
    
    // Soma local de votos
    int total_local = 0;
    for (int i = 0; i < CANDIDATOS; i++)
    {
        total_local += local[i];
    }

    // Soma global validada
    int total_global = 0;
    MPI_Reduce(
        &total_local,
        &total_global,
        1,
        MPI_INT,
        MPI_SUM,
        0,
        MPI_COMM_WORLD);

    double fim = MPI_Wtime();

    // =======================================================
    // 7. RESULTADO FINAL
    // =======================================================
    if (rank == 0)
    {
        printf("\n=== Resultado Final ===\n");

        int soma = 0;
        for (int i = 0; i < CANDIDATOS; i++)
        {
            printf("Candidato %d: %d votos\n", i, global[i]);
            soma += global[i];
        }

        printf("\nTotal: %d votos\n", soma);

        // Validação distribuída
        if (total_global == TOTAL_VOTOS)
        {
            printf("Consenso validado: todos os votos foram contabilizados.\n");
        }
        else
        {
            printf("Erro de consenso: inconsistência detectada.\n");
            printf("Esperado: %d | Recebido: %d\n", TOTAL_VOTOS, total_global);
        }
        printf("Tempo: %.4f segundos\n", fim - inicio);
    }

    MPI_Finalize();
    return 0;
}