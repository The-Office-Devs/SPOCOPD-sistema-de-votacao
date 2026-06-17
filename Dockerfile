FROM ubuntu:22.04

# =========================================
# Dependências de build e ferramentas
# =========================================
# No seu Dockerfile, altere a linha de apt-get install:
RUN apt-get update && \
    apt-get install -y \
    build-essential \
    wget \
    openssh-server \
    openssh-client \
    gnuplot-nox \
    zlib1g-dev \
    && apt-get clean

# =========================================
# Compilar Open MPI 5.0 com suporte ULFM
# =========================================
# A versão padrão do Ubuntu (openmpi-bin 4.1.x) NÃO inclui ULFM.
# É necessário compilar Open MPI 5.0+ que integra ULFM nativamente.
#
# Flags de runtime necessárias:
#   --mca mpi_ft_enable true
#
RUN wget https://download.open-mpi.org/release/open-mpi/v5.0/openmpi-5.0.6.tar.gz && \
    tar xzf openmpi-5.0.6.tar.gz && \
    cd openmpi-5.0.6 && \
    ./configure --prefix=/usr/local \
                --disable-man-pages \
                --enable-mpi-ext=ftmpi && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd / && rm -rf openmpi-5.0.6*

# =========================================
# Configuração SSH (necessário para MPI
# em múltiplos containers)
# =========================================

RUN mkdir -p /var/run/sshd

# senha root
RUN echo 'root:root' | chpasswd

# habilita login root
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' \
    /etc/ssh/sshd_config

# permitindo as chaves no ssh
RUN echo "PubkeyAuthentication yes" >> /etc/ssh/sshd_config

# habilita password auth
RUN sed -i 's/#PasswordAuthentication yes/PasswordAuthentication yes/' \
    /etc/ssh/sshd_config

# evita confirmação manual de host
RUN mkdir -p /root/.ssh && \
    echo "Host *" > /root/.ssh/config && \
    echo "    StrictHostKeyChecking no" >> /root/.ssh/config && \
    echo "    UserKnownHostsFile=/dev/null" >> /root/.ssh/config

# gera chave SSH automaticamente
RUN ssh-keygen -t rsa -N "" -f /root/.ssh/id_rsa

# autoriza a própria chave
RUN cat /root/.ssh/id_rsa.pub >> /root/.ssh/authorized_keys && \
    chmod 600 /root/.ssh/authorized_keys

# =========================================
# Aplicação MPI
# =========================================

WORKDIR /app

COPY votacao_otimizada.c .
COPY hosts /app/hosts
COPY benchmark.sh /app/benchmark.sh

RUN mpicc votacao_otimizada.c -O3 -o votacao_otimizada -lm

RUN chmod +x /app/benchmark.sh

EXPOSE 22

CMD ["/usr/sbin/sshd", "-D"]