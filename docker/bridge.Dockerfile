# syntax=docker/dockerfile:1.7
FROM ros:humble-ros-base

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONUNBUFFERED=1 \
    ROS_DOMAIN_ID=0

RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
        python3-pip \
        ros-humble-xacro \
        ros-humble-joint-state-publisher-gui \
        ros-humble-rviz2 \
    && rm -rf /var/lib/apt/lists/*

RUN useradd --create-home --uid 1000 --shell /bin/bash fydelis

WORKDIR /home/fydelis/ws

RUN chown -R fydelis:fydelis /home/fydelis/ws && \
    chmod -R u+w /home/fydelis/ws

USER fydelis

COPY --chown=fydelis:fydelis bridge/ ./src/fydelis_bridge/
COPY --chown=fydelis:fydelis description/ ./src/fydelisbot_description/
COPY docker/patches/ /tmp/patches/



# ✅ Primeiro constroi
RUN . /opt/ros/humble/setup.sh && \
    colcon build --install-base /home/fydelis/ws/install

# ✅ Depois cria o link em etapa separada
RUN mkdir -p /home/fydelis/ws/install/fydelis_bridge/lib/fydelis_bridge && \
    ln -sf /home/fydelis/ws/install/fydelis_bridge/bin/fydelis_bridge \
           /home/fydelis/ws/install/fydelis_bridge/lib/fydelis_bridge/fydelis_bridge

EXPOSE 9090

HEALTHCHECK --interval=5s --timeout=5s --retries=10 --start-period=15s \
    CMD . /opt/ros/humble/setup.sh && . /home/fydelis/ws/install/setup.bash && \
    ros2 topic list 2>/dev/null | grep -q /joint_states || exit 1

CMD ["/bin/bash", "-c", ". /opt/ros/humble/setup.sh && . /home/fydelis/ws/install/setup.bash && ros2 launch fydelisbot_description display.launch.py"]

