#!/bin/bash
# =========================================================================
# benchmark.sh — Execução automatizada para avaliação de desempenho
# =========================================================================
#
# Executa o sistema de votação distribuída com diferentes quantidades
# de processos e gera gráficos de escalabilidade automaticamente.
#
# Uso:
#   docker compose exec master /app/benchmark.sh
#
# =========================================================================

set -e

echo "============================================"
echo "  Benchmark — Sistema de Votação Distribuída"
echo "============================================"
echo ""

# Limpar métricas de execuções anteriores
rm -f /app/metricas.dat
rm -f /app/*.png

# Corrigir possíveis problemas de encoding no hostfile
sed -i 's/\r$//' /app/hosts

# Executar com diferentes quantidades de processos
for NP in 1 2 4 8; do
    echo "--------------------------------------------"
    echo "  Executando com $NP processo(s)..."
    echo "--------------------------------------------"

    mpirun --allow-run-as-root \
           -np $NP \
           --hostfile /app/hosts \
           --mca btl_tcp_if_include eth0 \
           --mca pml ob1 \
           --mca btl ^openib \
           --mca coll_tuned_use_dynamic_rules 1 \
           /app/votacao_otimizada

    echo ""
done

echo "============================================"
echo "  Benchmark concluído!"
echo "============================================"
echo ""
echo "Arquivos gerados:"
echo "  - metricas.dat          (dados de desempenho)"
echo "  - votos_regiao.csv      (resultados por região)"
echo "  - votos_por_regiao.png  (gráfico de distribuição)"
echo "  - desempenho_fases.png  (gráfico por fase)"
echo "  - speedup.png           (gráfico de escalabilidade)"
echo ""
echo "Para copiar os gráficos para o host:"
echo "  docker cp master:/app/votos_por_regiao.png ."
echo "  docker cp master:/app/desempenho_fases.png ."
echo "  docker cp master:/app/speedup.png ."
