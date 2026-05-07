#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TOTAL_VOTOS 100000000
#define CANDIDATOS 3

// TODO: Ajustar tratamento do resto da divisão
int main(int argc, char *argv[])
{
	int rank, size;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int votos_por_processo = TOTAL_VOTOS / size;
	int *votos_locais =malloc(votos_por_processo * sizeof(int));
	int local[CANDIDATOS] = {0};
	int global[CANDIDATOS] = {0};
	double inicio = MPI_Wtime();

	srand(time(NULL) + rank);

	// Geração local dos votos
	for (int i = 0; i < votos_por_processo; i++)
	{
		votos_locais[i] = rand() % CANDIDATOS;
	}

	// Contagem local
	for (int i = 0; i < votos_por_processo; i++)
	{
		int voto = votos_locais[i];

		local[voto]++;
	}

	// Reduce principal da eleição
	MPI_Reduce(
		local,
		global,
		CANDIDATOS,
		MPI_INT,
		MPI_SUM,
		0,
		MPI_COMM_WORLD);

	// =========================
	// CONSENSO BÁSICO
	// =========================

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

	if (rank == 0)
	{
		printf("\n=== Resultado Final ===\n");

		int soma = 0;
		for (int i = 0; i < CANDIDATOS; i++)
		{
			printf("Candidato %d: %d votos\n",i,global[i]);
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

			printf("Esperado: %d | Recebido: %d\n",TOTAL_VOTOS,total_global);
		}
		printf("Tempo: %.4f segundos\n", fim - inicio);
	}

	free(votos_locais);
	MPI_Finalize();
	return 0;
}