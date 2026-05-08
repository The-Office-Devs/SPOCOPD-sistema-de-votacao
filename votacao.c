#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TOTAL_VOTOS 155000000
#define CANDIDATOS 3

// TODO: Ajustar tratamento do resto da divisão
int main(int argc, char *argv[])
{
	int rank, size;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	char hostname[MPI_MAX_PROCESSOR_NAME];
	int hostname_len;

	MPI_Get_processor_name(
		hostname,
		&hostname_len
	);

	printf(
		"Rank %d executando em %s\n",
		rank,
		hostname
	);

	int votos_por_processo =
		TOTAL_VOTOS / size;

	// =========================
	// Buffers
	// =========================

	int *todos_votos = NULL;

	int *votos_locais = malloc(votos_por_processo * sizeof(int));
	int local[CANDIDATOS] = {0};
	int global[CANDIDATOS] = {0};
	double inicio = MPI_Wtime();

	// =========================
	// Rank 0 gera todos votos
	// =========================

	if (rank == 0)
	{
		todos_votos = malloc(TOTAL_VOTOS * sizeof(int));

		srand(time(NULL));

		for (int i = 0; i < TOTAL_VOTOS; i++)
		{
			todos_votos[i] = rand() % CANDIDATOS;
		}

		printf("\nRank 0 gerou %d votos\n",TOTAL_VOTOS);
	}

	// =========================
	// Distribuição dos votos
	// =========================

	MPI_Scatter(
		todos_votos,
		votos_por_processo,
		MPI_INT,
		votos_locais,
		votos_por_processo,
		MPI_INT,
		0,
		MPI_COMM_WORLD
	);

	// =========================
	// Contagem local
	// =========================

	for (int i = 0; i < votos_por_processo; i++)
	{
		int voto = votos_locais[i];

		local[voto]++;
	}

	// =========================
	// Replicação da contagem
	// =========================

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
				MPI_COMM_WORLD
			);
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
			MPI_STATUS_IGNORE
		);
	}

	// =========================
	// Reduce principal
	// =========================

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

	// =========================
	// Resultado final
	// =========================

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

	// =========================
	// Liberação memória
	// =========================

	free(votos_locais);
	if (rank == 0)
	{
		free(todos_votos);
	}
	MPI_Finalize();
	return 0;
}