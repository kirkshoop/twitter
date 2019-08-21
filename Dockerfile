FROM gitpod/workspace-full:latest

USER root
RUN apt-get update && apt-get install -y \
        glut \
        opengl \
        yum \
        yarn \
        zip \
        unzip \
        file \
        wget \
    && apt-get clean && rm -rf /var/cache/apt/* && rm -rf /var/lib/apt/lists/* && rm -rf /tmp/*

# Give back control
USER root