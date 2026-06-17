# Sistema de Votação Distribuída com MPI + ULFM

## Visão Geral

Sistema distribuído de apuração de votos baseado em **MPI** (Message Passing Interface) com arquitetura paralela, resiliente a falhas e capaz de gerar estatísticas avançadas por região geográfica.

### Funcionalidades

- ✓ Geração paralela de votos em todos os nós (sem gargalo no Rank 0)
- ✓ Estatísticas segmentadas por **5 regiões geográficas** (N, NE, CO, SE, S)
- ✓ Contabilização de votos **Sim**, **Não** e **Em Branco**
- ✓ **Tipos derivados MPI** para transmissão eficiente de structs
- ✓ **Replicação Active-Active** bidirecional entre processos vizinhos
- ✓ **Tolerância a falhas com ULFM** (revoke, shrink, recovery)
- ✓ **Recuperação automática** de dados via promoção de backups
- ✓ **Consenso distribuído** pós-falha com `MPIX_Comm_agree`
- ✓ Geração automática de **gráficos** com gnuplot
- ✓ Medição de desempenho por fase do pipeline

---

## Arquitetura

### Fluxo de Execução

```
1. Inicialização MPI + ULFM (MPI_Comm_dup, MPI_ERRORS_RETURN)
2. Geração paralela de votos (todos os ranks, carga balanceada)
3. Replicação Active-Active (MPI_Sendrecv bidirecional entre pares)
4. Redução com tipo derivado (MPI_Reduce + MPI_Op customizada)
5. Consenso distribuído (MPI_Allreduce + MPIX_Comm_agree)
6. [Se falha] → Revoke → Shrink → Recuperar backup → Consenso → Retry
7. Resultado final + Métricas + Gráficos
```

### Estrutura de Dados

```c
typedef struct {
    int regiao;        // 0=Norte, 1=Nordeste, 2=Centro-Oeste, 3=Sudeste, 4=Sul
    int votos_sim;     // Votos "Sim"
    int votos_nao;     // Votos "Não"
    int votos_branco;  // Votos "Em Branco"
} EstatisticaRegional;
```

### Replicação Active-Active

```
Rank 0 ↔ Rank 1  (backup recíproco)
Rank 2 ↔ Rank 3  (backup recíproco)
Rank 4 ↔ Rank 5  (backup recíproco)
Rank 6 ↔ Rank 7  (backup recíproco)
```

Se um rank falha, o parceiro promove o backup e continua a execução.

### Tolerância a Falhas (ULFM)

1. Operação MPI retorna erro (`MPIX_ERR_PROC_FAILED`)
2. `MPIX_Comm_revoke()` — invalida o comunicador
3. `MPIX_Comm_shrink()` — cria novo comunicador sem processos mortos
4. Verifica quem sobreviveu via `MPI_Allgather` de ranks originais
5. Promove backups dos processos falhos
6. Reconstrói malha de replicação
7. Executa consenso pós-recuperação
8. Retenta operação que falhou

---

## Como Executar

### Pré-requisitos

- Docker + Docker Compose

### Build e Execução do Cluster

```bash
# Build das imagens (inclui compilação do Open MPI 5.0 com ULFM)
docker compose up --build -d

# Executar com 8 processos
docker compose exec master mpirun \
  --allow-run-as-root \
  -np 8 \
  --hostfile /app/hosts \
  --mca mpi_ft_enable true \
  /app/votacao_otimizada

# Se der parse error no hostfile:
docker compose exec master sed -i 's/\r$//' /app/hosts
```

### Benchmark Automatizado

```bash
# Executa com 1, 2, 4 e 8 processos e gera gráficos
docker compose exec master /app/benchmark.sh

# Copiar gráficos para o host
docker cp master:/app/votos_por_regiao.png .
docker cp master:/app/desempenho_fases.png .
docker cp master:/app/speedup.png .
```

### Simulação de Falha

Para testar a tolerância a falhas, mate um container durante a execução:

```bash
# Terminal 1: Executar a aplicação
docker compose exec master mpirun \
  --allow-run-as-root -np 8 \
  --hostfile /app/hosts \
  --mca mpi_ft_enable true \
  /app/votacao_otimizada

# Terminal 2: Matar um worker durante a execução
docker kill worker3
```

---

## Configuração

| Parâmetro | Valor | Descrição |
|---|---|---|
| `TOTAL_VOTOS` | 155.000.000 | Quantidade total de votos |
| `NUM_REGIOES` | 5 | Regiões geográficas |
| Cluster | 8 nós | 1 master + 7 workers |

---

## Saída Esperada

```
[Rank 0] Gerou 19375000 votos (original rank 0)
[Rank 1] Gerou 19375000 votos (original rank 1)
...
[Rank 0] Backup bidirecional com Rank 1 estabelecido

========================================
       RESULTADO FINAL DA VOTAÇÃO
========================================
Processos ativos: 8

--- Norte ---
  Sim:      10333XXX votos
  Não:      10333XXX votos
  Branco:   10333XXX votos
  Total:    31000XXX votos

--- Nordeste ---
  ...

Total geral: 155000000 votos

✓ Consenso validado: todos os 155000000 votos contabilizados.
  Acordo ULFM: confirmado por todos os processos.

=== Métricas de Desempenho ===
Geração:    X.XXXX s
Replicação: X.XXXX s
Redução:    X.XXXX s
Consenso:   X.XXXX s
Total:      X.XXXX s

Gráficos gerados:
  - votos_por_regiao.png  (distribuição regional)
  - desempenho_fases.png  (tempo por fase)
  - speedup.png           (escalabilidade)
```

---

## Estrutura do Projeto

```
votacao/
├── votacao_otimizada.c   # Código principal (todas as 9 fases)
├── votacao.c             # Versão anterior (referência)
├── votacao_serial.c      # Versão serial (baseline para speedup)
├── dockerfile            # Imagem Docker com Open MPI 5.0 + ULFM + gnuplot
├── docker-compose.yml    # Orquestração do cluster (8 nós)
├── hosts                 # Lista de nós do cluster MPI
├── benchmark.sh          # Script de benchmark automatizado
├── build/                # Executáveis compilados
└── readme.md             # Este arquivo
```

---

## Gráficos Gerados

| Gráfico | Descrição |
|---|---|
| `votos_por_regiao.png` | Barras agrupadas: Sim/Não/Branco por região |
| `desempenho_fases.png` | Tempo de cada fase do pipeline |
| `speedup.png` | Tempo total vs. número de processos |
