FROM ubuntu:22.04

RUN apt-get update && apt-get install -y gcc make

WORKDIR /app
COPY . .

RUN make

# Expõe a porta padrão do servidor
EXPOSE 9090
