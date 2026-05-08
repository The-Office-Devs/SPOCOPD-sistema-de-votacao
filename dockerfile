FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y \
    openmpi-bin \
    libopenmpi-dev \
    openssh-server \
    openssh-client \
    gcc && \
    apt-get clean

# =========================
# Configuração SSH
# =========================

RUN mkdir -p /var/run/sshd

# senha root
RUN echo 'root:root' | chpasswd

# habilita login root
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' \
    /etc/ssh/sshd_config

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

# =========================
# Aplicação MPI
# =========================

WORKDIR /app

COPY votacao.c .

COPY hosts /app/hosts

RUN mpicc votacao.c -O3 -o votacao

EXPOSE 22

CMD ["/usr/sbin/sshd", "-D"]