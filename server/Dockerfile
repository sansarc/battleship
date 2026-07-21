FROM gcc:latest

WORKDIR /app

RUN apt-get update && apt-get install -y make cmake gdb

CMD ["make", "run"]