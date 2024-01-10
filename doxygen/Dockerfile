FROM alpine:3.18.4

# [--no-cache vs rm ...](https://stackoverflow.com/questions/49118579/alpine-dockerfile-advantages-of-no-cache-vs-rm-var-cache-apk)

RUN apk --update add --no-cache doxygen graphviz subversion git

CMD ["doxygen", "-v"]