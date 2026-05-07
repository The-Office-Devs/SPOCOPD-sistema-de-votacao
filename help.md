# Sistema de Votação Distribuída com MPI

## Estado Atual do Projeto

O sistema atual implementa um modelo de votação distribuída utilizando MPI (Message Passing Interface).

Cada processo MPI representa uma seção eleitoral independente, permitindo processamento paralelo da votação.

Fluxo atual:

1. Cada processo gera seus próprios votos localmente;
2. Cada processo realiza a contagem local;
3. Os resultados são agregados utilizando `MPI_Reduce`;
4. O processo `rank 0` consolida e exibe o resultado final.

## O que já foi implementado

O projeto já atende parcialmente os requisitos do exercício:

- votação simultânea em múltiplas seções eleitorais;
- processamento distribuído;
- paralelismo com MPI;
- agregação global dos resultados;
- medição de desempenho paralelo.

Cada processo funciona como uma seção eleitoral independente:

```text
rank 0 -> seção eleitoral 0
rank 1 -> seção eleitoral 1
rank 2 -> seção eleitoral 2
...
```
## Como rodar com docker

- Build da imagem: docker build -t mpi-votacao .

- Executar: docker run --rm mpi-votacao

## Como rodar local

- Compilar: mpicc votacao.c -O3 -o votacao
- Executar: mpirun -np 8 ./votacao