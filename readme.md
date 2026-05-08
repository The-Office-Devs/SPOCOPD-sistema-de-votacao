# Sistema de Votação Distribuída com MPI

## Estado Atual do Projeto

O sistema implementa um modelo robusto de **votação distribuída** utilizando **MPI** (Message Passing Interface) com suporte a **clusterização**, **consenso distribuído** e **replicação de dados**.

Cada processo MPI representa uma **seção eleitoral independente** em um cluster, permitindo processamento paralelo, fault tolerance e garantia de consistência.

### Fluxo de Funcionamento

1. **Geração de Votos**: Rank 0 gera `TOTAL_VOTOS` votos aleatoriamente
2. **Distribuição (Scatter)**: Votos distribuídos entre processos usando `MPI_Scatter`
3. **Contagem Local**: Cada processo conta votos para cada candidato
4. **Replicação**: Dados replicados entre pares de processos (backup)
5. **Agregação (Reduce)**: Resultados agregados com `MPI_Reduce`
6. **Consenso**: Validação de consistência com confirmação do total global
7. **Resultado Final**: Rank 0 consolida e exibe resultado com verificação

---

## O que já foi Implementado

- ✓ Votação simultância em múltiplas seções eleitorais
- ✓ Processamento distribuído com MPI
- ✓ **Clusterização**: Múltiplos nós via SSH/hostfile
- ✓ **Replicação de Dados**: Backup automático entre processos pares
- ✓ **Consenso Distribuído**: Validação de integridade dos dados
- ✓ Agregação global dos resultados
- ✓ Medição de desempenho paralelo
- ✓ Detecção de inconsistências

### Arquitetura dos Processos

Cada processo funciona como uma seção eleitoral independente em um cluster:

```
Rank 0 (líder)    → Gera votos, Coordena agregação
Rank 1 (réplica)  ← Recebe backup de Rank 0
Rank 2            → Processa votos
Rank 3 (réplica)  ← Recebe backup de Rank 2
  ⋮
```

### Estratégia de Replicação

- **Pares de Processos**: Ranks pares enviam dados para ranks ímpares
- **Backup Automático**: `rank % 2 == 0` → envia; `rank % 2 == 1` → recebe
- **Objetivo**: Garantir fault tolerance em caso de falha de um nó

### Mecanismo de Consenso

1. Cada processo soma seus votos locais
2. Total local é agregado globalmente via `MPI_Reduce`
3. Rank 0 valida: `total_global == TOTAL_VOTOS`
4. Se inconsistência detectada: alerta de erro de consenso


---

## Como Executar

### Com Docker (Single Node)

```bash
# Build da imagem
docker build -t mpi-votacao .

# Executar em um container
docker run --rm mpi-votacao
```

### Localmente

```bash
# Compilar
mpicc votacao.c -O3 -o ./build/votacao

# Executar com 8 processos
mpirun -np 8 ./build/votacao
```

### Com Docker Compose (Cluster Multi-Node)

```bash
# Build e inicializar containers no cluster
docker compose up --build -d

# Executar aplicação MPI no cluster
mpirun \
  --allow-run-as-root \
  -np 8 \
  --hostfile /app/hosts \
  /app/votacao
```

**Nota**: O arquivo `/app/hosts` contém a lista de nós do cluster.

---

## Configuração

- **TOTAL_VOTOS**: 100.000.000 votos
- **CANDIDATOS**: 3 candidatos
- **Distribuição**: Votos divididos equitativamente entre processos

---

## Saída Esperada

```
Rank 0 executando em node-0
Rank 1 executando em node-1
...
Rank 0 gerou 100000000 votos

=== Resultado Final ===
Candidato 0: 33333333 votos
Candidato 1: 33333334 votos
Candidato 2: 33333333 votos

Total: 100000000 votos
Consenso validado: todos os votos foram contabilizados.
Tempo: X.XXXX segundos
```

---

## Estrutura do Projeto

```
votacao/
├── votacao.c           # Código principal MPI
├── dockerfile          # Imagem Docker com MPI
├── docker-compose.yml  # Orquestração de containers
├── hosts               # Lista de nós do cluster
├── build/
│   └── votacao         # Executável compilado
└── readme.md             # Este arquivo
```
