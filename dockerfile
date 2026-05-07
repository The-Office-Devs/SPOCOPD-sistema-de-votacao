FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y \
    openmpi-bin \
    libopenmpi-dev \
    gcc && \
    apt-get clean

WORKDIR /app

COPY votacao.c .

RUN mpicc votacao.c -O3 -o votacao

CMD ["mpirun", "--allow-run-as-root", "-np", "8", "./votacao"]