FROM gitpod/workspace-full:latest

USER root
RUN apt-get update && apt-get install -y libglu1-mesa-dev freeglut3-dev mesa-common-dev && apt-get clean && rm -rf /var/cache/apt/* && rm -rf /var/lib/apt/lists/* && rm -rf /tmp/*

# Give back control
USER root
