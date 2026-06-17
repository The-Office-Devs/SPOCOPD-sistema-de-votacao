// =========================================================================
// Sistema de Votação Distribuída com MPI — Versão Evoluída
// =========================================================================
//
// Funcionalidades:
//   Fase 1: Estrutura de dados regional (Sim/Não/Branco por região)
//   Fase 2: Tipos derivados MPI com operação de redução customizada
//   Fase 3: Geração paralela sem gargalo no Rank 0
//   Fase 4: Replicação Active-Active bidirecional
//   Fase 5: Tolerância a falhas com ULFM (revoke/shrink)
//   Fase 6: Recuperação automática (promoção de backups)
//   Fase 7: Consenso distribuído pós-falha
//   Fase 8: Validação funcional integrada
//   Fase 9: Métricas de desempenho com geração de gráficos
//
// Compilação:
//   mpicc votacao_otimizada.c -O3 -o votacao_otimizada -lm
//
// Execução:
//   mpirun --allow-run-as-root -np 8 --hostfile hosts \
//          --mca mpi_ft_enable true /app/votacao_otimizada
//
// =========================================================================

#include <mpi.h>
#include <mpi-ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>
#include <unistd.h>

// =========================================================================
// Definições e Constantes
// =========================================================================
#define TOTAL_VOTOS 155000000
#define NUM_REGIOES 5

static const char *nome_regiao[] = {
    "Norte", "Nordeste", "Centro-Oeste", "Sudeste", "Sul"};

// =========================================================================
// Fase 1 — Estrutura de Dados Regional
// =========================================================================
typedef struct
{
    int regiao;       // ID da região (0..4)
    int votos_sim;    // Quantidade de votos "Sim"
    int votos_nao;    // Quantidade de votos "Não"
    int votos_branco; // Quantidade de votos "Em Branco"
} EstatisticaRegional;

// Variáveis globais para tipo MPI e operação customizada
static MPI_Datatype tipo_estatistica_mpi;
static MPI_Op op_soma_estatisticas;

// =========================================================================
// Fase 2 — Criação do Tipo Derivado MPI
// =========================================================================

/**
 * Cria um MPI_Datatype correspondente à struct EstatisticaRegional.
 * Permite transmissão eficiente da estrutura entre processos sem
 * serialização manual, respeitando alinhamento e offsets.
 */
void criar_tipo_estatistica(void)
{
    int block_lengths[4] = {1, 1, 1, 1};
    MPI_Datatype types[4] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT};
    MPI_Aint offsets[4];

    offsets[0] = offsetof(EstatisticaRegional, regiao);
    offsets[1] = offsetof(EstatisticaRegional, votos_sim);
    offsets[2] = offsetof(EstatisticaRegional, votos_nao);
    offsets[3] = offsetof(EstatisticaRegional, votos_branco);

    MPI_Type_create_struct(4, block_lengths, offsets, types, &tipo_estatistica_mpi);
    MPI_Type_commit(&tipo_estatistica_mpi);
}

/**
 * Operação de redução customizada para EstatisticaRegional.
 * Soma os campos de votos (sim, não, branco) sem alterar o campo região.
 * Compatível com MPI_Reduce e MPI_Allreduce.
 */
void fn_somar_estatisticas(void *invec, void *inoutvec, int *len, MPI_Datatype *dtype)
{
    EstatisticaRegional *in = (EstatisticaRegional *)invec;
    EstatisticaRegional *inout = (EstatisticaRegional *)inoutvec;

    for (int i = 0; i < *len; i++)
    {
        inout[i].votos_sim += in[i].votos_sim;
        inout[i].votos_nao += in[i].votos_nao;
        inout[i].votos_branco += in[i].votos_branco;
        // regiao permanece inalterada (já inicializada corretamente)
    }
}

// =========================================================================
// Fase 5 — Tolerância a Falhas com ULFM
// =========================================================================

/**
 * Repara o comunicador após detecção de falha de processo.
 *
 * Fluxo:
 *   1. Revoga o comunicador atual (sinaliza a todos os sobreviventes)
 *   2. Executa shrink (operação coletiva que cria novo comunicador
 *      contendo apenas os processos vivos)
 *   3. Configura tratamento de erros no novo comunicador
 *   4. Atualiza rank e size
 *
 * Retorna 1 em caso de sucesso, 0 em falha crítica.
 */
int reparar_comunicador(MPI_Comm *comm, int *rank, int *size)
{
    MPI_Comm novo_comm;
    int rc;

    // Revogar — informa a todos os processos que o comunicador é inválido.
    // Qualquer operação MPI pendente neste comunicador retornará erro.
    MPIX_Comm_revoke(*comm);

    // Shrink — cria novo comunicador sem os processos mortos.
    // Esta é uma operação coletiva: todos os sobreviventes participam.
    rc = MPIX_Comm_shrink(*comm, &novo_comm);
    if (rc != MPI_SUCCESS)
    {
        fprintf(stderr, "[ULFM] Falha crítica ao realizar shrink do comunicador\n");
        return 0;
    }

    // Liberar comunicador antigo (nunca liberar MPI_COMM_WORLD)
    if (*comm != MPI_COMM_WORLD)
    {
        MPI_Comm_free(comm);
    }

    *comm = novo_comm;
    MPI_Comm_rank(*comm, rank);
    MPI_Comm_size(*comm, size);

    // Habilitar tratamento de erros no novo comunicador
    MPI_Comm_set_errhandler(*comm, MPI_ERRORS_RETURN);

    printf("[ULFM] Comunicador reparado. Novo rank=%d, novo size=%d\n", *rank, *size);
    return 1;
}

/**
 * Verifica se o parceiro original de backup sobreviveu após um shrink.
 *
 * Realiza um MPI_Allgather dos ranks originais de todos os sobreviventes,
 * e verifica se o rank original do parceiro está entre eles.
 *
 * Retorna 1 se o parceiro está vivo, 0 caso contrário.
 */
int verificar_parceiro_vivo(MPI_Comm comm, int rank_original_local,
                            int parceiro_original, int size_atual)
{
    // MPI_PROC_NULL significa sem parceiro (rank órfão)
    if (parceiro_original == MPI_PROC_NULL)
        return 0;

    int *ranks_originais = malloc(size_atual * sizeof(int));
    if (!ranks_originais)
        return 0;

    MPI_Allgather(&rank_original_local, 1, MPI_INT,
                  ranks_originais, 1, MPI_INT, comm);

    int parceiro_vivo = 0;
    for (int i = 0; i < size_atual; i++)
    {
        if (ranks_originais[i] == parceiro_original)
        {
            parceiro_vivo = 1;
            break;
        }
    }

    free(ranks_originais);
    return parceiro_vivo;
}

// =========================================================================
// Fase 6 — Recuperação Automática dos Dados
// =========================================================================

/**
 * Promove dados de backup quando o parceiro morre e reestabelece
 * a malha de replicação no comunicador reduzido.
 *
 * Se o parceiro morreu:
 *   - Os dados do backup são incorporados aos dados locais
 *   - O backup antigo é zerado
 *
 * Em seguida, novos pares são calculados e uma nova replicação
 * bidirecional é executada.
 */
void recuperar_dados(EstatisticaRegional *local, EstatisticaRegional *backup,
                     int parceiro_morreu, int rank, int size, MPI_Comm comm)
{
    // Promoção: incorporar dados do parceiro falho
    if (parceiro_morreu)
    {
        printf("[Rank %d] Promovendo backup do parceiro falho para dados ativos\n", rank);
        for (int r = 0; r < NUM_REGIOES; r++)
        {
            local[r].votos_sim += backup[r].votos_sim;
            local[r].votos_nao += backup[r].votos_nao;
            local[r].votos_branco += backup[r].votos_branco;
        }
        // Limpar backup antigo — será substituído pela nova replicação
        memset(backup, 0, sizeof(EstatisticaRegional) * NUM_REGIOES);
    }

    // Recalcular parceiro no comunicador reduzido
    int novo_parceiro;
    if (rank % 2 == 0)
        novo_parceiro = (rank + 1 < size) ? rank + 1 : MPI_PROC_NULL;
    else
        novo_parceiro = rank - 1;

    // Reestabelecer replicação bidirecional com novo parceiro
    EstatisticaRegional novo_backup[NUM_REGIOES];
    memset(novo_backup, 0, sizeof(novo_backup));

    MPI_Sendrecv(local, NUM_REGIOES, tipo_estatistica_mpi, novo_parceiro, 0,
                 novo_backup, NUM_REGIOES, tipo_estatistica_mpi, novo_parceiro, 0,
                 comm, MPI_STATUS_IGNORE);

    memcpy(backup, novo_backup, sizeof(EstatisticaRegional) * NUM_REGIOES);

    if (rank == 0)
        printf("[Recuperação] Malha de backups reconstruída com %d processos\n", size);
}

// =========================================================================
// Fase 7 — Consenso Distribuído Pós-Falha
// =========================================================================

/**
 * Executa protocolo de consenso entre todos os processos sobreviventes.
 *
 * Verifica:
 *   1. Quantidade de processos ativos (todos concordam no size)
 *   2. Total de votos válidos (soma global == TOTAL_VOTOS)
 *   3. Acordo formal ULFM (MPIX_Comm_agree)
 *
 * Retorna 1 se consenso obtido, 0 se inconsistência detectada.
 */
int consenso_pos_recuperacao(MPI_Comm comm, int rank, int size,
                             EstatisticaRegional *local)
{
    // 1. Verificar concordância sobre quantidade de processos ativos
    int meu_size = size;
    int consenso_size;
    MPI_Allreduce(&meu_size, &consenso_size, 1, MPI_INT, MPI_MIN, comm);

    if (consenso_size != size)
    {
        if (rank == 0)
            printf("[CONSENSO] Divergência no tamanho: local=%d, consenso=%d\n",
                   size, consenso_size);
        return 0;
    }

    // 2. Verificar concordância sobre total de votos
    int total_local = 0;
    for (int r = 0; r < NUM_REGIOES; r++)
        total_local += local[r].votos_sim + local[r].votos_nao + local[r].votos_branco;

    int total_global = 0;
    MPI_Allreduce(&total_local, &total_global, 1, MPI_INT, MPI_SUM, comm);

    if (total_global != TOTAL_VOTOS)
    {
        if (rank == 0)
            printf("[CONSENSO] FALHA: esperado %d votos, encontrado %d\n",
                   TOTAL_VOTOS, total_global);
        return 0;
    }

    // 3. Acordo formal ULFM — barreira de consenso
    int flag = 1;
    MPIX_Comm_agree(comm, &flag);

    if (!flag)
    {
        if (rank == 0)
            printf("[CONSENSO] FALHA: acordo ULFM não obtido\n");
        return 0;
    }

    if (rank == 0)
        printf("[CONSENSO] OK: %d processos ativos, %d votos validados\n",
               consenso_size, total_global);

    return 1;
}

// =========================================================================
// Fase 9 — Métricas de Desempenho e Geração de Gráficos
// =========================================================================

/**
 * Salva métricas de desempenho em arquivo para análise posterior.
 * Formato: num_procs t_geracao t_replicacao t_reduce t_consenso t_total
 * O arquivo é aberto em modo append para acumular dados de múltiplas execuções.
 */
void salvar_metricas(const char *arquivo, int num_procs,
                     double t_geracao, double t_replicacao,
                     double t_reduce, double t_consenso, double t_total)
{
    FILE *f = fopen(arquivo, "a");
    if (!f)
        return;
    fprintf(f, "%d %.6f %.6f %.6f %.6f %.6f\n",
            num_procs, t_geracao, t_replicacao, t_reduce, t_consenso, t_total);
    fclose(f);
}

/**
 * Salva os resultados de votação por região em formato CSV.
 */
void salvar_votos_csv(const char *arquivo, EstatisticaRegional *global)
{
    FILE *f = fopen(arquivo, "w");
    if (!f)
        return;
    fprintf(f, "Regiao,Sim,Nao,Branco,Total\n");
    for (int r = 0; r < NUM_REGIOES; r++)
    {
        int total = global[r].votos_sim + global[r].votos_nao + global[r].votos_branco;
        fprintf(f, "%s,%d,%d,%d,%d\n",
                nome_regiao[r], global[r].votos_sim, global[r].votos_nao,
                global[r].votos_branco, total);
    }
    fclose(f);
}

/**
 * Gera scripts gnuplot e executa automaticamente para produzir gráficos PNG.
 *
 * Gráficos gerados:
 *   1. votos_por_regiao.png — Distribuição de votos Sim/Não/Branco por região
 *   2. desempenho_fases.png — Tempo de execução por fase do pipeline
 *   3. speedup.png — Escalabilidade vs. número de processos (acumulativo)
 */
void gerar_graficos(const char *arquivo_metricas, const char *arquivo_votos,
                    int num_procs, double t_geracao, double t_replicacao,
                    double t_reduce, double t_consenso)
{
    FILE *f;

    // -----------------------------------------------------------------
    // Gráfico 1: Distribuição de votos por região (barras agrupadas)
    // -----------------------------------------------------------------
    f = fopen("grafico_votos.gnuplot", "w");
    if (f)
    {
        fprintf(f, "set terminal png size 1400,700 enhanced font 'Arial,13'\n");
        fprintf(f, "set output 'votos_por_regiao.png'\n");
        fprintf(f, "set title 'Distribuição de Votos por Região Geográfica' font ',16'\n");
        fprintf(f, "set style data histogram\n");
        fprintf(f, "set style histogram cluster gap 1\n");
        fprintf(f, "set style fill solid 0.8 border -1\n");
        fprintf(f, "set boxwidth 0.9\n");
        fprintf(f, "set xlabel 'Região'\n");
        fprintf(f, "set ylabel 'Quantidade de Votos'\n");
        fprintf(f, "set format y '%%.0s %%c'\n");
        fprintf(f, "set grid ytics\n");
        fprintf(f, "set key outside right top\n");
        fprintf(f, "set datafile separator ','\n");
        fprintf(f, "set xtic rotate by -20 scale 0\n");
        fprintf(f, "plot '%s' using 2:xtic(1) every ::1 title 'Sim' lc rgb '#2ecc71', \\\n",
                arquivo_votos);
        fprintf(f, "     '' using 3 every ::1 title 'Não' lc rgb '#e74c3c', \\\n");
        fprintf(f, "     '' using 4 every ::1 title 'Branco' lc rgb '#95a5a6'\n");
        fclose(f);
        system("gnuplot grafico_votos.gnuplot 2>/dev/null");
    }

    // -----------------------------------------------------------------
    // Gráfico 2: Tempo de execução por fase (barras simples)
    // -----------------------------------------------------------------
    FILE *fb = fopen("desempenho_breakdown.dat", "w");
    if (fb)
    {
        fprintf(fb, "Fase Tempo\n");
        fprintf(fb, "Geracao %.6f\n", t_geracao);
        fprintf(fb, "Replicacao %.6f\n", t_replicacao);
        fprintf(fb, "Reducao %.6f\n", t_reduce);
        fprintf(fb, "Consenso %.6f\n", t_consenso);
        fclose(fb);
    }

    f = fopen("grafico_desempenho.gnuplot", "w");
    if (f)
    {
        fprintf(f, "set terminal png size 1200,700 enhanced font 'Arial,13'\n");
        fprintf(f, "set output 'desempenho_fases.png'\n");
        fprintf(f, "set title 'Tempo de Execução por Fase (%d processos)' font ',16'\n", num_procs);
        fprintf(f, "set style data histogram\n");
        fprintf(f, "set style histogram cluster gap 1\n");
        fprintf(f, "set style fill solid 0.8 border -1\n");
        fprintf(f, "set boxwidth 0.7\n");
        fprintf(f, "set xlabel 'Fase'\n");
        fprintf(f, "set ylabel 'Tempo (segundos)'\n");
        fprintf(f, "set grid ytics\n");
        fprintf(f, "set xtic rotate by -20 scale 0\n");
        fprintf(f, "plot 'desempenho_breakdown.dat' using 2:xtic(1) every ::1 "
                    "title 'Tempo' lc rgb '#3498db'\n");
        fclose(f);
        system("gnuplot grafico_desempenho.gnuplot 2>/dev/null");
    }

    // -----------------------------------------------------------------
    // Gráfico 3: Speedup / Escalabilidade (acumulativo entre execuções)
    // -----------------------------------------------------------------
    f = fopen("grafico_speedup.gnuplot", "w");
    if (f)
    {
        fprintf(f, "set terminal png size 1200,700 enhanced font 'Arial,13'\n");
        fprintf(f, "set output 'speedup.png'\n");
        fprintf(f, "set title 'Escalabilidade — Tempo Total vs. Número de Processos' font ',16'\n");
        fprintf(f, "set xlabel 'Número de Processos'\n");
        fprintf(f, "set ylabel 'Tempo Total (segundos)'\n");
        fprintf(f, "set grid\n");
        fprintf(f, "set key outside right top\n");
        fprintf(f, "set style data linespoints\n");
        fprintf(f, "set pointsize 1.5\n");
        fprintf(f, "plot '%s' using 1:6 title 'Tempo Total' lw 2 pt 7 lc rgb '#e74c3c', \\\n",
                arquivo_metricas);
        fprintf(f, "     '%s' using 1:2 title 'Geração' lw 1 pt 5 lc rgb '#2ecc71', \\\n",
                arquivo_metricas);
        fprintf(f, "     '%s' using 1:3 title 'Replicação' lw 1 pt 5 lc rgb '#3498db', \\\n",
                arquivo_metricas);
        fprintf(f, "     '%s' using 1:4 title 'Redução' lw 1 pt 5 lc rgb '#f39c12', \\\n",
                arquivo_metricas);
        fprintf(f, "     '%s' using 1:5 title 'Consenso' lw 1 pt 5 lc rgb '#9b59b6'\n",
                arquivo_metricas);
        fclose(f);
        system("gnuplot grafico_speedup.gnuplot 2>/dev/null");
    }

    printf("\nGráficos gerados:\n");
    printf("  - votos_por_regiao.png  (distribuição regional)\n");
    printf("  - desempenho_fases.png  (tempo por fase)\n");
    printf("  - speedup.png           (escalabilidade)\n");
}

// =========================================================================
// Função Principal
// =========================================================================
int main(int argc, char *argv[])
{
    int rank, size, rank_original;
    MPI_Comm comm_atual;
    int rc;

    // -----------------------------------------------------------------
    // Inicialização MPI + ULFM (Fase 5)
    // -----------------------------------------------------------------
    MPI_Init(&argc, &argv);

    MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN);

    MPI_Barrier(comm_atual); // Sincroniza todos antes de começar a votação

    // Duplicar MPI_COMM_WORLD — necessário para ULFM, pois não se pode
    // revogar MPI_COMM_WORLD diretamente
    MPI_Comm_dup(MPI_COMM_WORLD, &comm_atual);
    MPI_Comm_rank(comm_atual, &rank);
    MPI_Comm_size(comm_atual, &size);

    // Salvar rank original para rastreamento após possíveis shrinks
    rank_original = rank;

    // Habilitar retorno de erros ao invés de abort (requisito ULFM)
    MPI_Comm_set_errhandler(comm_atual, MPI_ERRORS_RETURN);

    // Criar tipo derivado MPI e operação de redução customizada (Fase 2)
    criar_tipo_estatistica();
    MPI_Op_create(fn_somar_estatisticas, 1, &op_soma_estatisticas);

    double t_inicio_total = MPI_Wtime();

    // =================================================================
    // FASE 1 + 3: Geração Paralela de Votos
    // =================================================================
    //
    // Cada rank calcula sua carga de trabalho independentemente:
    //   minha_carga = base + 1 (se rank < resto)
    //
    // Os votos excedentes (resto da divisão) são distribuídos entre os
    // primeiros 'resto' ranks, eliminando o gargalo do Rank 0.
    // =================================================================
    int votos_por_processo = TOTAL_VOTOS / size;
    int resto = TOTAL_VOTOS % size;
    int minha_carga = votos_por_processo + (rank < resto ? 1 : 0);

    // Inicializar estatísticas locais por região
    EstatisticaRegional local[NUM_REGIOES];
    memset(local, 0, sizeof(local));
    for (int r = 0; r < NUM_REGIOES; r++)
        local[r].regiao = r;

    // Semente distribuída — cada rank gera sequência pseudoaleatória
    // independente, evitando correlação entre processos
    srand(time(NULL) + (rank * 1000));

    double t_geracao = MPI_Wtime();

    for (int i = 0; i < minha_carga; i++)
    {
        int regiao = rand() % NUM_REGIOES;
        int tipo = rand() % 3; // 0=Sim, 1=Não, 2=Branco

        if (tipo == 0)
            local[regiao].votos_sim++;
        else if (tipo == 1)
            local[regiao].votos_nao++;
        else
            local[regiao].votos_branco++;
    }

    t_geracao = MPI_Wtime() - t_geracao;

    printf("[Rank %d] Gerou %d votos (original rank %d)\n",
           rank, minha_carga, rank_original);

    // =================================================================
    // FASE 4: Replicação Active-Active (Bidirecional)
    // =================================================================
    //
    // Cada par de processos adjacentes (0↔1, 2↔3, ...) troca dados
    // reciprocamente usando MPI_Sendrecv. Ambos ficam com backup do
    // parceiro, eliminando a vulnerabilidade da replicação unidirecional.
    //
    // Ranks órfãos (último rank em clusters com nº ímpar de processos)
    // utilizam MPI_PROC_NULL — o Sendrecv se torna no-op para eles.
    // =================================================================
    int parceiro_original;
    if (rank % 2 == 0)
        parceiro_original = (rank + 1 < size) ? rank + 1 : MPI_PROC_NULL;
    else
        parceiro_original = rank - 1;

    EstatisticaRegional backup[NUM_REGIOES];
    memset(backup, 0, sizeof(backup));
    int backup_valido = 0;

    double t_replicacao = MPI_Wtime();
    
    rc = MPI_Sendrecv(
        local, NUM_REGIOES, tipo_estatistica_mpi, parceiro_original, 0,
        backup, NUM_REGIOES, tipo_estatistica_mpi, parceiro_original, 0,
        comm_atual, MPI_STATUS_IGNORE);

    // ULFM: Verificar falha durante replicação
    if (rc != MPI_SUCCESS)
    {
        MPIX_Comm_revoke(comm_atual);

        printf("[Rank %d] Falha detectada durante replicação. Iniciando recuperação ULFM...\n",
               rank_original);

        if (reparar_comunicador(&comm_atual, &rank, &size))
        {
            int parceiro_vivo = verificar_parceiro_vivo(
                comm_atual, rank_original, parceiro_original, size);

            recuperar_dados(local, backup, !parceiro_vivo, rank, size, comm_atual);

            if (!consenso_pos_recuperacao(comm_atual, rank, size, local))
            {
                if (rank == 0)
                    printf("[ERRO FATAL] Consenso falhou após recuperação na replicação\n");
                MPI_Abort(comm_atual, 1);
            }
        }
        else
        {
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
    else
    {
        backup_valido = 1;
        if (parceiro_original != MPI_PROC_NULL)
            printf("[Rank %d] Backup bidirecional com Rank %d estabelecido\n",
                   rank, parceiro_original);
        else
            printf("[Rank %d] Rank órfão — sem parceiro de backup\n", rank);
    }

    t_replicacao = MPI_Wtime() - t_replicacao;

    // =================================================================
    // FASE 2: Redução com Tipo Derivado MPI
    // =================================================================
    //
    // Utiliza MPI_Reduce com o tipo derivado tipo_estatistica_mpi e a
    // operação customizada op_soma_estatisticas para agregar os
    // resultados de todos os ranks no rank 0.
    // =================================================================
    EstatisticaRegional global[NUM_REGIOES];
    memset(global, 0, sizeof(global));
    for (int r = 0; r < NUM_REGIOES; r++)
        global[r].regiao = r;

    double t_reduce = MPI_Wtime();

    rc = MPI_Reduce(
        local, global, NUM_REGIOES,
        tipo_estatistica_mpi, op_soma_estatisticas,
        0, comm_atual);

    // ULFM: Verificar falha durante redução
    if (rc != MPI_SUCCESS)
    {
        MPIX_Comm_revoke(comm_atual);

        printf("[Rank %d] Falha detectada durante redução. Iniciando recuperação ULFM...\n",
               rank_original);

        if (reparar_comunicador(&comm_atual, &rank, &size))
        {
            int parceiro_vivo = verificar_parceiro_vivo(
                comm_atual, rank_original, parceiro_original, size);

            if (!parceiro_vivo && backup_valido)
                recuperar_dados(local, backup, 1, rank, size, comm_atual);
            else
                recuperar_dados(local, backup, 0, rank, size, comm_atual);

            if (!consenso_pos_recuperacao(comm_atual, rank, size, local))
            {
                if (rank == 0)
                    printf("[ERRO FATAL] Consenso falhou após recuperação na redução\n");
                MPI_Abort(comm_atual, 1);
            }

            // Retry: refazer a redução com o comunicador reparado
            memset(global, 0, sizeof(global));
            for (int r = 0; r < NUM_REGIOES; r++)
                global[r].regiao = r;

            MPI_Reduce(local, global, NUM_REGIOES,
                       tipo_estatistica_mpi, op_soma_estatisticas,
                       0, comm_atual);
        }
        else
        {
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    t_reduce = MPI_Wtime() - t_reduce;

    // =================================================================
    // FASE 7: Consenso Distribuído
    // =================================================================
    //
    // Verifica que a soma total de votos processados por todos os ranks
    // corresponde exatamente a TOTAL_VOTOS. Utiliza MPI_Allreduce para
    // que todos os processos tenham acesso ao total validado, e
    // MPIX_Comm_agree como barreira formal de consenso ULFM.
    // =================================================================
    double t_consenso = MPI_Wtime();

    int total_local = 0;
    for (int r = 0; r < NUM_REGIOES; r++)
        total_local += local[r].votos_sim + local[r].votos_nao + local[r].votos_branco;

    int total_global = 0;

    rc = MPI_Allreduce(&total_local, &total_global, 1, MPI_INT, MPI_SUM, comm_atual);

    // ULFM: Verificar falha durante consenso
    if (rc != MPI_SUCCESS)
    {
        MPIX_Comm_revoke(comm_atual);

        printf("[Rank %d] Falha detectada durante consenso. Iniciando recuperação ULFM...\n",
               rank_original);

        if (reparar_comunicador(&comm_atual, &rank, &size))
        {
            int parceiro_vivo = verificar_parceiro_vivo(
                comm_atual, rank_original, parceiro_original, size);

            if (!parceiro_vivo && backup_valido)
                recuperar_dados(local, backup, 1, rank, size, comm_atual);
            else
                recuperar_dados(local, backup, 0, rank, size, comm_atual);

            // Retry: refazer consenso com comunicador reparado
            total_local = 0;
            for (int r = 0; r < NUM_REGIOES; r++)
                total_local += local[r].votos_sim + local[r].votos_nao + local[r].votos_branco;

            MPI_Allreduce(&total_local, &total_global, 1, MPI_INT, MPI_SUM, comm_atual);

            // Retry: refazer redução com dados atualizados
            memset(global, 0, sizeof(global));
            for (int r = 0; r < NUM_REGIOES; r++)
                global[r].regiao = r;

            MPI_Reduce(local, global, NUM_REGIOES,
                       tipo_estatistica_mpi, op_soma_estatisticas,
                       0, comm_atual);
        }
        else
        {
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    // Acordo formal ULFM — todos os sobreviventes devem concordar
    int flag_acordo = 1;
    MPIX_Comm_agree(comm_atual, &flag_acordo);

    t_consenso = MPI_Wtime() - t_consenso;

    double t_total = MPI_Wtime() - t_inicio_total;

    // =================================================================
    // RESULTADO FINAL + MÉTRICAS (Fases 8 e 9)
    // =================================================================
    if (rank == 0)
    {
        printf("\n========================================\n");
        printf("       RESULTADO FINAL DA VOTAÇÃO       \n");
        printf("========================================\n");
        printf("Processos ativos: %d\n\n", size);

        int soma_total = 0;
        for (int r = 0; r < NUM_REGIOES; r++)
        {
            int total_regiao = global[r].votos_sim +
                               global[r].votos_nao +
                               global[r].votos_branco;
            soma_total += total_regiao;

            printf("--- %s ---\n", nome_regiao[r]);
            printf("  Sim:     %10d votos\n", global[r].votos_sim);
            printf("  Não:     %10d votos\n", global[r].votos_nao);
            printf("  Branco:  %10d votos\n", global[r].votos_branco);
            printf("  Total:   %10d votos\n\n", total_regiao);
        }

        printf("Total geral: %d votos\n\n", soma_total);

        // Validação de consenso (Fase 8)
        if (total_global == TOTAL_VOTOS && flag_acordo)
        {
            printf("✓ Consenso validado: todos os %d votos contabilizados.\n",
                   TOTAL_VOTOS);
            printf("  Acordo ULFM: confirmado por todos os processos.\n");
        }
        else
        {
            printf("✗ Erro de consenso: inconsistência detectada.\n");
            printf("  Esperado: %d | Recebido: %d | Acordo ULFM: %s\n",
                   TOTAL_VOTOS, total_global, flag_acordo ? "Sim" : "Não");
        }

        // Métricas de desempenho (Fase 9)
        printf("\n=== Métricas de Desempenho ===\n");
        printf("Geração:    %.4f s\n", t_geracao);
        printf("Replicação: %.4f s\n", t_replicacao);
        printf("Redução:    %.4f s\n", t_reduce);
        printf("Consenso:   %.4f s\n", t_consenso);
        printf("Total:      %.4f s\n", t_total);

        // Salvar dados e gerar gráficos automaticamente
        salvar_metricas("metricas.dat", size,
                        t_geracao, t_replicacao, t_reduce, t_consenso, t_total);
        salvar_votos_csv("votos_regiao.csv", global);
        gerar_graficos("metricas.dat", "votos_regiao.csv",
                       size, t_geracao, t_replicacao, t_reduce, t_consenso);
    }

    // =================================================================
    // Cleanup
    // =================================================================
    MPI_Op_free(&op_soma_estatisticas);
    MPI_Type_free(&tipo_estatistica_mpi);
    if (comm_atual != MPI_COMM_WORLD)
        MPI_Comm_free(&comm_atual);
    MPI_Finalize();

    return 0;
}