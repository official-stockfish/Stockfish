# build it
FROM gcc as build
WORKDIR /stockfish
COPY ./src .
RUN make build ARCH=x86-64 
#copy on a small image
FROM ubuntu:latest
WORKDIR /stockfish
COPY --from=build /stockfish/stockfish .
ENTRYPOINT [ "./stockfish" ] 
